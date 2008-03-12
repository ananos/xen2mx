/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/timer.h>

#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_peer.h"
#include "omx_endpoint.h"
#include "omx_region.h"
#ifndef OMX_DISABLE_SHARED
#include "omx_shared.h"
#endif

/**************************
 * Pull-specific Constants
 */

#define OMX_PULL_BLOCK_LENGTH_MAX (OMX_PULL_REPLY_LENGTH_MAX*OMX_PULL_REPLY_PER_BLOCK)

#define OMX_PULL_RETRANSMIT_TIMEOUT_MS	1000
#define OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES (OMX_PULL_RETRANSMIT_TIMEOUT_MS*HZ/1000)

#ifdef OMX_MX_WIRE_COMPAT
#if OMX_PULL_REPLY_LENGTH_MAX >= 65536
#error Cannot store rdma offsets > 65535 in 16bits offsets on the wire
#endif
#endif

/**********************
 * Pull-specific Types
 */

/* use a bitmask type large enough to store two pull frame blocks */
#if OMX_PULL_REPLY_PER_BLOCK > 15
typedef uint64_t omx_frame_bitmask_t;
#elif OMX_PULL_REPLY_PER_BLOCK > 7
typedef uint32_t omx_frame_bitmask_t;
#else
typedef uint16_t omx_frame_bitmask_t;
#endif

enum omx_pull_handle_status {
	/*
	 * The handle is normal, being processed as usual and its timeout handler is running ok.
	 * It is queued on the endpoint running_list.
	 */
	OMX_PULL_HANDLE_STATUS_OK,

	/*
	 * The handle has been removed from the idr, but the timeout handler is still running.
	 * It is queued on the endpoint done_but_timer_list.
	 * Either the pull has completed (or aborted on error), or the endpoint is being closed and
	 * all handles have been scheduled for removal.
	 * The timeout handler must exit next time it runs. It will release the reference on the handle,
	 * dequeue it, and the handle may be destroyed.
	 */
	OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT,

	/*
	 * The handle has been removed from the idr and the endpoint lists, and its timeout handler has exited
	 * and released its reference.
	 * Either the pull has completed (or aborted on error) and the handler caught the MUST_EXIT status,
	 * or the timeout was reached and its handler aborted the handle directly.
	 */
	OMX_PULL_HANDLE_STATUS_TIMER_EXITED,
};

struct omx_pull_block_desc {
	uint32_t frame_index;
	uint32_t block_length;
	uint32_t first_frame_offset;
};

struct omx_pull_handle {
	struct kref refcount;
	struct list_head list_elt; /* always queued on one of the endpoint lists */
	int idr_index; /* that's what idr_get_new wants */
	uint32_t magic; /* 8bits endpoint index + 24bits handle generation number */

	/* timer for retransmission */
	struct timer_list retransmit_timer;
	uint64_t last_retransmit_jiffies;

	/* global pull fields */
	struct omx_endpoint * endpoint;
	struct omx_user_region * region;
	uint64_t lib_cookie;
	uint32_t total_length;
	uint32_t puller_rdma_offset;
	uint32_t pulled_rdma_offset;

	/* current status */
	spinlock_t lock;
	enum omx_pull_handle_status status;
	uint32_t remaining_length;
	uint32_t frame_index; /* index of the first requested frame */
	uint32_t next_frame_index; /* index of the frame to request */
	uint32_t block_frames; /* number of frames requested */
	omx_frame_bitmask_t frames_missing_bitmap; /* frames not received at all */
	unsigned int frames_copying_nr; /* frames received but not copied yet */
	struct omx_pull_block_desc first_desc;
	struct omx_pull_block_desc second_desc;
	uint32_t already_requeued_first; /* the first block has been requested again since the last timer */

	/* pull packet header */
	struct omx_hdr pkt_hdr;
};

static void omx_pull_handle_timeout_handler(unsigned long data);

/*
 * Notes about locking:
 *
 * Each handle owns a spin_lock that protects the actual pull status (frame index, ...).
 * It also protects its handle status and its queueing in the endpoint lists and idr.
 * This lock is always taken *before* the endpoint pull handle lock.
 *
 * The handle does not own a reference on the endpoint. It is always queued in one
 * endpoint lists, and the endpoint closing will enforce its detruction.
 * When the endpoint starts to be closed, it calls prepare_exit which sets all handle
 * to timer_must_exit but cannot wait for them because of the possible interrupt
 * context. Later, the cleanup thread will cleanup the endpoint, including destroying
 * the handle timers that are still running.
 *
 * The pile of handles for an endpoint is protected by a rwlock. It is taken for
 * reading when acquiring an handle (when a pull reply or nack mcp arrives, likely
 * in a bottom half). It is taken for writing when creating a handle (when the
 * application request a pull), finishing a handle (when a pull reply arrives
 * and completes the pull request, likely in a bottom half), and when destroying
 * and remaining handles (when an endpoint is closed).
 * Since a bottom half and the application may both acquire the rwlock for
 * writing, we must always disable bottom halves when taking the rwlock for
 * either read or writing.
 */

/*
 * Notes about retransmission:
 *
 * The puller request 2 blocks of data, and waits for OMX_PULL_REPLY_PER_BLOCK
 * replies for each of them.
 *
 * A timer is set to detect when nothing has been received for a while.  It is
 * updated every time a new reply is received. This timer repost requests to
 * get current blocks (using descriptors that were cached in the pull handle).
 *
 * Additionally, if the second block completes before the first one, there is
 * a good chance that one packet got lost for the first block. In this case,
 * we optimistically re-request the first block.
 * To avoid re-requesting too often, we do it only once per timeout.
 *
 * In the end, the timer is only called if:
 * + one packet is lost for both the first and the second current block
 * + or one packet is missing in the first block after one optimistic re-request.
 * So the timeout doesn't need to be short, 1 second is enough.
 */

#ifdef OMX_DRIVER_DEBUG
/* defined as module parameters */
extern unsigned long omx_PULL_REQ_packet_loss;
extern unsigned long omx_PULL_REPLY_packet_loss;
/* index between 0 and the above limit */
static unsigned long omx_PULL_REQ_packet_loss_index = 0;
static unsigned long omx_PULL_REPLY_packet_loss_index = 0;
#endif /* OMX_DRIVER_DEBUG */

/********************************
 * Pull handle bitmap management
 */

#if OMX_PULL_REPLY_PER_BLOCK >= 32
#error Cannot request more than 32 replies per pull block
#endif

#define OMX_PULL_HANDLE_BLOCK_BITMASK ((((omx_frame_bitmask_t)1)<<OMX_PULL_REPLY_PER_BLOCK)-1)
#define OMX_PULL_HANDLE_SECOND_BLOCK_BITMASK (OMX_PULL_HANDLE_BLOCK_BITMASK<<OMX_PULL_REPLY_PER_BLOCK)
#define OMX_PULL_HANDLE_BOTH_BLOCKS_BITMASK ((((omx_frame_bitmask_t)1)<<(2*OMX_PULL_REPLY_PER_BLOCK))-1)

/* both blocks are done and there are no more block to request (but some copy may be pending) */
#define OMX_PULL_HANDLE_ALL_BLOCKS_DONE(handle) \
	(!((handle)->remaining_length) \
	 && !((handle)->frames_missing_bitmap & OMX_PULL_HANDLE_BOTH_BLOCKS_BITMASK))

/* first requested block got all its frames (but some copy may be pending) */
#define OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle) \
	(!((handle)->frames_missing_bitmap & OMX_PULL_HANDLE_BLOCK_BITMASK))

/* first requested block got all its frames (but some copy may be pending) */
#define OMX_PULL_HANDLE_SECOND_BLOCK_DONE(handle) \
	(!((handle)->frames_missing_bitmap & OMX_PULL_HANDLE_SECOND_BLOCK_BITMASK))

static INLINE void
omx_pull_handle_append_needed_frames(struct omx_pull_handle * handle,
				     uint32_t block_length,
				     uint32_t first_frame_offset)
{
	omx_frame_bitmask_t new_mask;
	int new_frames;

	new_frames = (first_frame_offset + block_length
		      + OMX_PULL_REPLY_LENGTH_MAX-1) / OMX_PULL_REPLY_LENGTH_MAX;
	BUG_ON(new_frames + handle->block_frames > 64);

	new_mask = ((((omx_frame_bitmask_t)1) << new_frames) - 1) << handle->block_frames;

	handle->frames_missing_bitmap |= new_mask;
	handle->block_frames += new_frames;
	handle->next_frame_index += new_frames;
}

static INLINE void
omx_pull_handle_first_block_done(struct omx_pull_handle * handle)
{
	uint32_t first_block_frames = handle->block_frames > OMX_PULL_REPLY_PER_BLOCK
	 ? handle->block_frames - OMX_PULL_REPLY_PER_BLOCK : handle->block_frames;

	handle->frames_missing_bitmap >>= first_block_frames;
	handle->frame_index += first_block_frames;
	handle->block_frames -= first_block_frames;
	memcpy(&handle->first_desc, &handle->second_desc,
	       sizeof(struct omx_pull_block_desc));
	handle->already_requeued_first = 0;
}

/**********************************
 * Pull handle acquiring/releasing
 */

/*
 * Acquire a handle.
 *
 * Either another reference on this handle should be owned,
 * or the endpoint lock should be hold.
 */
static INLINE void
omx_pull_handle_acquire(struct omx_pull_handle * handle)
{
	kref_get(&handle->refcount);
}

/*
 * Actual freeing of a handle when the last reference is released
 */
static void
__omx_pull_handle_last_release(struct kref * kref)
{
	struct omx_pull_handle * handle = container_of(kref, struct omx_pull_handle, refcount);

	dprintk(KREF, "releasing the last reference on pull handle %p\n",
		handle);

	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_EXITED);
	kfree(handle);
}

/*
 * Release an acquired pull handle
 */
static INLINE void
omx_pull_handle_release(struct omx_pull_handle * handle)
{
	kref_put(&handle->refcount, __omx_pull_handle_last_release);
}


/***************************************
 * Per-endpoint pull handles management
 */

int
omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint)
{
	INIT_LIST_HEAD(&endpoint->pull_handles_running_list);
	INIT_LIST_HEAD(&endpoint->pull_handles_done_but_timer_list);
	idr_init(&endpoint->pull_handles_idr);
	rwlock_init(&endpoint->pull_handles_lock);
	return 0;
}

/*
 * Called when the last reference on the endpoint is removed,
 * possibly from unsafe context, cannot del_timer_sync() then.
 */
void
omx_endpoint_pull_handles_prepare_exit(struct omx_endpoint * endpoint)
{
	/*
	 * ask all pull handles of the endpoint to stop their timer.
	 * but we can't take endpoint->pull_handles_lock before handle->lock since that would deadlock
	 * so we use a tricky loop to take locks in order
	 */

	write_lock_bh(&endpoint->pull_handles_lock);
	while (!list_empty(&endpoint->pull_handles_running_list)) {
		struct omx_pull_handle * handle;

		/* get the first handle of the list, acquire it and release the list lock */
		handle = list_first_entry(&endpoint->pull_handles_running_list, struct omx_pull_handle, list_elt);
		omx_pull_handle_acquire(handle);
		write_unlock_bh(&endpoint->pull_handles_lock);

		/* take the handle lock and check the status in case it changed while the lock was released */
		spin_lock_bh(&handle->lock);
		if (handle->status == OMX_PULL_HANDLE_STATUS_OK) {
			/* the handle didn't change, do our stuff */
			handle->status = OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT;

			/*
			 * remove from the idr so that no incoming packet can find it anymore,
			 * and move to the done_but_timer list
			 */
			dprintk(PULL, "moving handle %p to the done_but_timer list and removing from idr\n", handle);
			write_lock(&endpoint->pull_handles_lock);
			idr_remove(&endpoint->pull_handles_idr, handle->idr_index);
			list_move(&handle->list_elt, &endpoint->pull_handles_done_but_timer_list);
			write_unlock(&endpoint->pull_handles_lock);
		} else {
			/* the handle has been moved out of the list, it means that the timer is done, nothing to done */
		}
		spin_unlock_bh(&handle->lock);
		omx_pull_handle_release(handle);

		write_lock_bh(&endpoint->pull_handles_lock);
	}
	write_unlock_bh(&endpoint->pull_handles_lock);

	idr_destroy(&endpoint->pull_handles_idr);
}

/*
 * Called when cleaning the endpoint, always from the cleanup thread, may del_timer_sync() then.
 */
void
omx_endpoint_pull_handles_force_exit(struct omx_endpoint * endpoint)
{
	might_sleep();

	write_lock_bh(&endpoint->pull_handles_lock);
	while (!list_empty(&endpoint->pull_handles_done_but_timer_list)) {
		struct omx_pull_handle * handle;
		int ret;

		/* get the first handle of the list, acquire it and release the list lock */
		handle = list_first_entry(&endpoint->pull_handles_done_but_timer_list, struct omx_pull_handle, list_elt);
		omx_pull_handle_acquire(handle);
		write_unlock_bh(&endpoint->pull_handles_lock);

		dprintk(PULL, "stopping handle %p timer with del_sync_timer\n", handle);
		ret = del_timer_sync(&handle->retransmit_timer);
		spin_lock_bh(&handle->lock);
		if (ret) {
			dprintk(PULL, "del_timer_sync stopped pull handle %p timer\n", handle);

			/* we deactivated the timer, cleanup ourself */
			BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT);
			handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;

			/* drop from the list */
			write_lock(&endpoint->pull_handles_lock);
			list_del(&handle->list_elt);
			write_unlock(&endpoint->pull_handles_lock);

			spin_unlock_bh(&handle->lock);
			/* release the timer reference */
			omx_pull_handle_release(handle);

		} else {
			dprintk(PULL, "del_timer_sync was useless pull handle %p timer, already exited\n", handle);

			/* the timer expired, nothing to do */
			BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_EXITED);

			spin_unlock_bh(&handle->lock);
		}

		omx_pull_handle_release(handle);
		write_lock_bh(&endpoint->pull_handles_lock);
	}
	write_unlock_bh(&endpoint->pull_handles_lock);
}

/******************************
 * Endpoint pull-magic management
 */

/* the magic is an incremented 24bits generation followed by 8bits for the endpoint index */
#define OMX_PULL_HANDLE_MAGIC_ENDPOINT_INDEX_BITS 8
#if OMX_ENDPOINT_INDEX_MAX > (1<<OMX_PULL_HANDLE_MAGIC_ENDPOINT_INDEX_BITS)
#error Endpoint index may not fit in the pull handle magic
#endif

#define OMX_PULL_HANDLE_MAGIC_XOR 0x21071980

static uint32_t omx_endpoint_magic_generation = 0;

static INLINE uint32_t
omx_generate_pull_magic(struct omx_endpoint * endpoint)
{
	omx_endpoint_magic_generation++;
	return ((omx_endpoint_magic_generation << OMX_PULL_HANDLE_MAGIC_ENDPOINT_INDEX_BITS)
		| endpoint->endpoint_index)
	       ^ OMX_PULL_HANDLE_MAGIC_XOR;
}

/*
 * Acquire an endpoint using a pull handle magic given on the wire.
 *
 * Returns an endpoint acquired, on ERR_PTR(-errno) on error
 */
static INLINE struct omx_endpoint *
omx_endpoint_acquire_by_pull_magic(struct omx_iface * iface, uint32_t magic)
{
	uint8_t index = (magic ^ OMX_PULL_HANDLE_MAGIC_XOR) & ((1UL << OMX_PULL_HANDLE_MAGIC_ENDPOINT_INDEX_BITS) - 1);
	return omx_endpoint_acquire_by_iface_index(iface, index);
}

/************************
 * Pull handles creation
 */

static INLINE int
omx_pull_handle_pkt_hdr_fill(struct omx_endpoint * endpoint,
			     struct omx_pull_handle * handle,
			     struct omx_cmd_pull * cmd)
{
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct omx_hdr * mh = &handle->pkt_hdr;
	struct ethhdr * eh = &mh->head.eth;
	struct omx_pkt_pull_request * pull_n = &mh->body.pull;
	int ret;

	/* pre-fill the packet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd->peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in pull request header\n");
		goto out;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(pull_n->ptype, OMX_PKT_TYPE_PULL);
	OMX_PKT_FIELD_FROM(pull_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(pull_n->dst_endpoint, cmd->dest_endpoint);
	OMX_PKT_FIELD_FROM(pull_n->session, cmd->session_id);
	OMX_PKT_FIELD_FROM(pull_n->total_length, handle->total_length);
	OMX_PKT_FIELD_FROM(pull_n->pulled_rdma_id, cmd->remote_rdma_id);
	OMX_PKT_FIELD_FROM(pull_n->pulled_rdma_seqnum, cmd->remote_rdma_seqnum);
	OMX_PKT_FIELD_FROM(pull_n->pulled_rdma_offset, handle->pulled_rdma_offset);
	OMX_PKT_FIELD_FROM(pull_n->src_pull_handle, handle->idr_index);
	OMX_PKT_FIELD_FROM(pull_n->src_magic, handle->magic);

	/* block_length, frame_index, and first_frame_offset filled at actual send */

	return 0;

 out:
	return ret;
}

/*
 * Create a pull handle and return it as acquired and locked.
 */
static INLINE struct omx_pull_handle *
omx_pull_handle_create(struct omx_endpoint * endpoint,
		       struct omx_cmd_pull * cmd)
{
	struct omx_pull_handle * handle;
	struct omx_user_region * region;
	int err;

	/* acquire the region */
	region = omx_user_region_acquire(endpoint, cmd->local_rdma_id);
	if (unlikely(!region)) {
		err = -ENOMEM;
		goto out;
	}

	/* alloc the pull handle */
	handle = kmalloc(sizeof(struct omx_pull_handle), GFP_KERNEL);
	if (unlikely(!handle)) {
		printk(KERN_INFO "Open-MX: Failed to allocate a pull handle\n");
		goto out_with_region;
	}
	/* initialize the lock, we will acquire it soon */
	spin_lock_init(&handle->lock);

	/* while failed, realloc and retry */
 idr_try_alloc:
	err = idr_pre_get(&endpoint->pull_handles_idr, GFP_KERNEL);
	if (unlikely(!err)) {
		printk(KERN_ERR "Open-MX: Failed to allocate idr space for pull handles\n");
		err = -ENOMEM;
		goto out_with_handle;
	}

	/* lock the handle lock first we'll need it later and can't take it after the endpoint lock */
	spin_lock(&handle->lock);

	write_lock_bh(&endpoint->pull_handles_lock);
	err = idr_get_new(&endpoint->pull_handles_idr, handle, &handle->idr_index);
	if (unlikely(err == -EAGAIN)) {
		write_unlock_bh(&endpoint->pull_handles_lock);
		spin_unlock(&handle->lock);
		goto idr_try_alloc;
	}

	/* we are good now, finish filling the handle */
	kref_init(&handle->refcount);
	handle->endpoint = endpoint;
	handle->region = region;
	handle->lib_cookie = cmd->lib_cookie;
	handle->total_length = cmd->length;
	handle->puller_rdma_offset = cmd->local_offset;
	handle->pulled_rdma_offset = cmd->remote_offset;

	/* initialize variable stuff */
	handle->status = OMX_PULL_HANDLE_STATUS_OK;
	handle->remaining_length = cmd->length;
	handle->frame_index = 0;
	handle->next_frame_index = 0;
	handle->block_frames = 0;
	handle->frames_missing_bitmap = 0;
	handle->frames_copying_nr = 0;
	handle->already_requeued_first = 0;
	handle->last_retransmit_jiffies = cmd->resend_timeout_jiffies + jiffies;
	handle->magic = omx_generate_pull_magic(endpoint);

	/* initialize cached header */
	err = omx_pull_handle_pkt_hdr_fill(endpoint, handle, cmd);
	if (err < 0)
		goto out_with_idr;

	/* init timer */
	setup_timer(&handle->retransmit_timer, omx_pull_handle_timeout_handler,
		    (unsigned long) handle);

	/* queue in the endpoint list */
	list_add_tail(&handle->list_elt,
		      &endpoint->pull_handles_running_list);

	write_unlock_bh(&endpoint->pull_handles_lock);

	dprintk(PULL, "created and acquired pull handle %p\n", handle);

	/* tell the sparse checker that we keep the lock and pass it back to the caller */
	__release(&handle->lock);

	return handle;

 out_with_idr:
	idr_remove(&endpoint->pull_handles_idr, handle->idr_index);
	write_unlock_bh(&endpoint->pull_handles_lock);
	spin_unlock(&handle->lock);
 out_with_handle:
	kfree(handle);
 out_with_region:
	omx_user_region_release(region);
 out:
	return NULL;
}

/*************************
 * Pull handle completion
 */

/*
 * Takes an acquired and locked pull handle and complete it.
 * Called by the BH after receiving a pull reply or a nack.
 */
static INLINE void
omx_pull_handle_done_release(struct omx_pull_handle * handle)
{
	struct omx_user_region * region = handle->region;
	struct omx_endpoint * endpoint = handle->endpoint;

	/* tell the sparse checker that the caller took the lock */
	__acquire(&handle->lock);

	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);
	handle->status = OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT;

	/* remove from the idr (and endpoint list) so that no incoming packet can find it anymore */
	dprintk(PULL, "moving done handle %p to the done_but_timer list and removing from idr\n", handle);
	write_lock_bh(&endpoint->pull_handles_lock);
	idr_remove(&endpoint->pull_handles_idr, handle->idr_index);
	list_move(&handle->list_elt, &endpoint->pull_handles_done_but_timer_list);
	write_unlock_bh(&endpoint->pull_handles_lock);

	spin_unlock(&handle->lock);

	/* release the region and handle */
	omx_user_region_release(region);
	omx_pull_handle_release(handle);
}

/*
 * Takes an acquired and locked pull handle and complete it.
 * Called by the retransmit timer when expired.
 */
static INLINE void
omx_pull_handle_timeout_release(struct omx_pull_handle * handle)
{
	struct omx_user_region * region = handle->region;
	struct omx_endpoint * endpoint = handle->endpoint;

	/* tell the sparse checker that the caller took the lock */
	__acquire(&handle->lock);

	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);
	handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;

	/* remove from the idr (and endpoint list) so that no incoming packet can find it anymore */
	dprintk(PULL, "pull handle %p timer done, removing from idr and endpoint list\n", handle);
	write_lock_bh(&endpoint->pull_handles_lock);
	idr_remove(&endpoint->pull_handles_idr, handle->idr_index);
	list_del(&handle->list_elt);
	write_unlock_bh(&endpoint->pull_handles_lock);

	spin_unlock(&handle->lock);

	/* release the region and handle */
	omx_user_region_release(region);
	omx_pull_handle_release(handle);
}

/*
 * Takes a locked and acquired pull handle
 * and report an event to user-space.
 *
 * May be called by the BH after receiving a pull reply or a nack,
 * and by the retransmit timer when expired.
 */
static void
omx_pull_handle_done_notify(struct omx_pull_handle * handle,
			    uint8_t status)
{
	struct omx_endpoint *endpoint = handle->endpoint;
	struct omx_evt_pull_done event;

	/* notify event */
	event.status = status;
	event.lib_cookie = handle->lib_cookie;
	event.pulled_length = handle->total_length - handle->remaining_length;
	event.local_rdma_id = handle->region->id;
	omx_notify_exp_event(endpoint,
			     OMX_EVT_PULL_DONE,
			     &event, sizeof(event));
}

/************************
 * Sending pull requests
 */

/* Called with the handle acquired and locked */
static INLINE struct sk_buff *
omx_fill_pull_block_request(struct omx_pull_handle * handle,
			    struct omx_pull_block_desc * desc)
{
	struct omx_iface * iface = handle->endpoint->iface;
	uint32_t frame_index = desc->frame_index;
	uint32_t block_length = desc->block_length;
	uint32_t first_frame_offset = desc->first_frame_offset;
	struct sk_buff * skb;
	struct omx_hdr * mh;
	struct omx_pkt_pull_request * pull_n;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_request);

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create pull skb\n");
		return ERR_PTR(-ENOMEM);
	}

	/* locate headers */
	mh = omx_hdr(skb);
	pull_n = &mh->body.pull;

	/* copy common pkt hdrs from the handle */
	memcpy(mh, &handle->pkt_hdr, sizeof(handle->pkt_hdr));

	OMX_PKT_FIELD_FROM(pull_n->block_length, block_length);
	OMX_PKT_FIELD_FROM(pull_n->first_frame_offset, first_frame_offset);
	OMX_PKT_FIELD_FROM(pull_n->frame_index, frame_index);

	omx_send_dprintk(&mh->head.eth, "PULL handle %lx magic %lx length %ld out of %ld, frame index %ld first_frame_offset %ld",
			 (unsigned long) OMX_FROM_PKT_FIELD(pull_n->src_pull_handle),
			 (unsigned long) OMX_FROM_PKT_FIELD(pull_n->src_magic),
			 (unsigned long) block_length,
			 (unsigned long) OMX_FROM_PKT_FIELD(pull_n->total_length),
			 (unsigned long) frame_index,
			 (unsigned long) first_frame_offset);

	return skb;
}

int
omx_ioctl_pull(struct omx_endpoint * endpoint,
	       void __user * uparam)
{
	struct omx_cmd_pull cmd;
	struct omx_pull_handle * handle;
	struct omx_iface * iface = endpoint->iface;
	struct sk_buff * skb, * skb2;
	uint32_t block_length, first_frame_offset;
	int err = 0;

	err = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send pull cmd hdr\n");
		err = -EFAULT;
		goto out;
	}

	/* check the offsets */
	if (cmd.local_offset >= OMX_PULL_REPLY_LENGTH_MAX
	    || cmd.remote_offset >= OMX_PULL_REPLY_LENGTH_MAX) {
		err = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_pull(endpoint, &cmd);
#endif

	/* create, acquire and lock the handle */
	handle = omx_pull_handle_create(endpoint, &cmd);
	if (unlikely(!handle)) {
		printk(KERN_INFO "Open-MX: Failed to allocate a pull handle\n");
		err = -ENOMEM;
		goto out;
	}

	/* tell the sparse checker that the lock has been taken by omx_pull_handle_create() */
	__acquire(&handle->lock);

	/* send a first pull block request */
	block_length = OMX_PULL_BLOCK_LENGTH_MAX
		- handle->pulled_rdma_offset;
	if (block_length > handle->remaining_length)
		block_length = handle->remaining_length;
	handle->remaining_length -= block_length;

	first_frame_offset = handle->pulled_rdma_offset;

	handle->first_desc.frame_index = handle->next_frame_index;
	handle->first_desc.block_length = block_length;
	handle->first_desc.first_frame_offset = first_frame_offset;
	skb = omx_fill_pull_block_request(handle, &handle->first_desc);
	if (unlikely(IS_ERR(skb))) {
		BUG_ON(PTR_ERR(skb) != -ENOMEM);
		/* just ignore the memory allocation failure and let retransmission take care of it */
		skb = NULL;
	}

	omx_pull_handle_append_needed_frames(handle,
					     block_length, first_frame_offset);

	/* send a second pull block request if needed */
	skb2 = NULL;
	if (!handle->remaining_length)
		goto skbs_ready;

	block_length = OMX_PULL_BLOCK_LENGTH_MAX;
	if (block_length > handle->remaining_length)
		block_length = handle->remaining_length;
	handle->remaining_length -= block_length;

	handle->second_desc.frame_index = handle->next_frame_index;
	handle->second_desc.block_length = block_length;
	handle->second_desc.first_frame_offset = 0;
	skb2 = omx_fill_pull_block_request(handle, &handle->second_desc);
	if (unlikely(IS_ERR(skb2))) {
		BUG_ON(PTR_ERR(skb2) != -ENOMEM);
		/* just ignore the memory allocation failure and let retransmission take care of it */
		skb2 = NULL;
	}

	omx_pull_handle_append_needed_frames(handle, block_length, 0);

 skbs_ready:
	/* schedule the timeout handler now that we are ready to send the requests */
	__mod_timer(&handle->retransmit_timer,
		    jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	/*
	 * do not keep the lock while sending
	 * since the loopback device may cause reentrancy
	 */
	spin_unlock(&handle->lock);

	if (likely(skb))
		omx_queue_xmit(iface, skb, PULL_REQ);
	if (likely(skb2))
		omx_queue_xmit(iface, skb2, PULL_REQ);

	return 0;

 out:
	return err;
}

/*************************
 * Handle timeout handler
 */

static INLINE void
omx_progress_pull_on_handle_timeout_handle_locked(struct omx_iface * iface,
						  struct omx_pull_handle * handle)
{
	struct sk_buff * skb = NULL, * skb2 = NULL;

	/* tell the sparse checker that the lock has been taken by the caller */
	__acquire(&handle->lock);

	/* request the first block again */
	omx_counter_inc(iface, PULL_TIMEOUT_HANDLER_FIRST_BLOCK);

	skb = omx_fill_pull_block_request(handle, &handle->first_desc);
	if (unlikely(IS_ERR(skb))) {
		BUG_ON(PTR_ERR(skb) != -ENOMEM);
		skb = NULL;
	} else
		handle->already_requeued_first = 0;

	/*
	 * If the second block isn't done either, request it again
	 * (otherwise the 2-block pipeline would be broken for ever)
	 * This shouldn't happen often since it means a packet has been lost
	 * in both first and second blocks.
	 */
	if (!OMX_PULL_HANDLE_SECOND_BLOCK_DONE(handle)) {
		omx_counter_inc(iface, PULL_TIMEOUT_HANDLER_SECOND_BLOCK);

		skb2 = omx_fill_pull_block_request(handle, &handle->second_desc);
		if (unlikely(IS_ERR(skb2))) {
			BUG_ON(PTR_ERR(skb2) != -ENOMEM);
			skb2 = NULL;
		}
	}

	/* reschedule another timeout handler */
	mod_timer(&handle->retransmit_timer,
		  jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	/*
	 * do not keep the lock while sending
	 * since the loopback device may cause reentrancy
	 */
	spin_unlock(&handle->lock);

	if (likely(skb))
		omx_queue_xmit(iface, skb, PULL_REQ);
	if (likely(skb2))
		omx_queue_xmit(iface, skb2, PULL_REQ);
}

/*
 * Retransmission callback, owns a reference on the handle.
 * Running as long as status is OMX_PULL_HANDLE_STATUS_OK.
 */
static void
omx_pull_handle_timeout_handler(unsigned long data)
{
	struct omx_pull_handle * handle = (void *) data;
	struct omx_endpoint * endpoint = handle->endpoint;
	struct omx_iface * iface = endpoint->iface;

	dprintk(PULL, "pull handle %p timer reached, might need to request again\n", handle);

	spin_lock(&handle->lock);

	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT);
		handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;

		dprintk(PULL, "pull handle %p timer exiting\n", handle);

		/*
		 * the handle has been moved to the done_but_timer_list,
		 * it's already outside of the idr, no need to lock bh
		 */
		write_lock(&endpoint->pull_handles_lock);
		list_del(&handle->list_elt);
		write_unlock(&endpoint->pull_handles_lock);

		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		return; /* timer will never be called again (status is TIMER_EXITED) */
	}

	if (jiffies > handle->last_retransmit_jiffies) {
		omx_counter_inc(iface, PULL_TIMEOUT_ABORT);
		dprintk(PULL, "pull handle %p last retransmit time reached, reporting an error\n", handle);
		omx_pull_handle_done_notify(handle, OMX_EVT_PULL_DONE_TIMEOUT);
		omx_pull_handle_timeout_release(handle);
		/* tell the sparse checker that the lock has been released by omx_pull_handle_timeout_release() */
		__release(&handle->lock);

		return; /* timer will never be called again (status is TIMER_EXITED) */
	}

	BUG_ON(OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle));

	/* request more replies if necessary */
	omx_progress_pull_on_handle_timeout_handle_locked(iface, handle);
	/* tell sparse checker that the lock has been released by omx_progress_pull_on_handle_timeout_handle_locked() */
	__release(&handle->lock);
}

/*******************************************
 * Recv pull requests and send pull replies
 */

/* pull reply skb destructor to release the user region */
static void
omx_send_pull_reply_skb_destructor(struct sk_buff *skb)
{
	struct omx_user_region * region = omx_get_skb_destructor_data(skb);
	omx_user_region_release(region);
}

int
omx_recv_pull_request(struct omx_iface * iface,
		      struct omx_hdr * pull_mh,
		      struct sk_buff * orig_skb)
{
	struct net_device * ifp = iface->eth_ifp;
	struct omx_endpoint * endpoint;
	struct ethhdr *pull_eh = &pull_mh->head.eth;
	struct omx_pkt_pull_request *pull_request_n = &pull_mh->body.pull;
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(pull_request_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(pull_request_n->src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(pull_request_n->session);
	uint32_t block_length = OMX_FROM_PKT_FIELD(pull_request_n->block_length);
	uint32_t src_pull_handle = OMX_FROM_PKT_FIELD(pull_request_n->src_pull_handle);
	uint32_t src_magic = OMX_FROM_PKT_FIELD(pull_request_n->src_magic);
	uint32_t frame_index = OMX_FROM_PKT_FIELD(pull_request_n->frame_index);
	uint32_t first_frame_offset = OMX_FROM_PKT_FIELD(pull_request_n->first_frame_offset);
	uint32_t pulled_rdma_id = OMX_FROM_PKT_FIELD(pull_request_n->pulled_rdma_id);
	uint32_t pulled_rdma_offset = OMX_FROM_PKT_FIELD(pull_request_n->pulled_rdma_offset);
	uint16_t peer_index = OMX_FROM_PKT_FIELD(pull_mh->head.dst_src_peer_index);
	struct omx_user_region_offset_cache region_cache;
	struct omx_pkt_pull_reply *pull_reply_n;
	struct omx_hdr *reply_mh;
	struct ethhdr *reply_eh;
	size_t reply_hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	struct omx_user_region *region;
	uint32_t current_frame_seqnum, current_msg_offset, block_remaining_length;
	int replies, i;
	int err = 0;

	omx_counter_inc(iface, RECV_PULL_REQ);

        /* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(pull_eh, "PULL packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(pull_eh, "PULL packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_mcp(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  src_endpoint, src_pull_handle, src_magic);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(pull_eh, "PULL packet with bad session");
		omx_send_nack_mcp(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  src_endpoint, src_pull_handle, src_magic);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(pull_eh, "PULL handle %lx magic %lx length %ld out of %ld, index %ld first_frame_offset %ld",
			 (unsigned long) src_pull_handle,
			 (unsigned long) src_magic,
			 (unsigned long) block_length,
			 (unsigned long) OMX_FROM_PKT_FIELD(pull_request_n->total_length),
			 (unsigned long) frame_index,
			 (unsigned long) first_frame_offset);

	/* compute and check the number of PULL_REPLY to send */
	replies = (first_frame_offset + block_length
		   + OMX_PULL_REPLY_LENGTH_MAX-1) / OMX_PULL_REPLY_LENGTH_MAX;
	if (unlikely(replies > OMX_PULL_REPLY_PER_BLOCK)) {
		omx_counter_inc(iface, DROP_PULL_BAD_REPLIES);
		omx_drop_dprintk(pull_eh, "PULL packet for %d REPLY (%d max)",
				 replies, OMX_PULL_REPLY_PER_BLOCK);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* get the rdma window once */
	region = omx_user_region_acquire(endpoint, pulled_rdma_id);
	if (unlikely(!region)) {
		omx_counter_inc(iface, DROP_PULL_BAD_REGION);
		omx_drop_dprintk(pull_eh, "PULL packet with bad region");
		omx_send_nack_mcp(iface, peer_index,
				  OMX_NACK_TYPE_BAD_RDMAWIN,
				  src_endpoint, src_pull_handle, src_magic);
		goto out_with_endpoint;
	}

	/* initialize pull reply fields */
	current_frame_seqnum = frame_index;
	current_msg_offset = frame_index * OMX_PULL_REPLY_LENGTH_MAX
		- pulled_rdma_offset + first_frame_offset;
	block_remaining_length = block_length;

	/* initialize the region offset cache and check length/offset */
	err = omx_user_region_offset_cache_init(region, &region_cache,
						current_msg_offset + pulled_rdma_offset, block_length);
	if (err < 0) {
		omx_counter_inc(iface, DROP_PULL_BAD_OFFSET_LENGTH);
		omx_drop_dprintk(pull_eh, "PULL packet due to wrong offset/length");
		/* no nack but the wire proto should be fixed for this */
		err = -EINVAL;
		goto out_with_region;
	}

	/* send all replies */
	for(i=0; i<replies; i++) {
		struct sk_buff *skb;
		uint32_t frame_length;

		frame_length = (i==0)
			? OMX_PULL_REPLY_LENGTH_MAX - first_frame_offset
			: OMX_PULL_REPLY_LENGTH_MAX;
		if (block_remaining_length < frame_length)
			frame_length = block_remaining_length;

		if (unlikely(frame_length <= omx_skb_copy_max
			     || reply_hdr_len + frame_length < ETH_ZLEN
			     || !omx_skb_frags))
			goto linear;

		/* allocate a skb */
		skb = omx_new_skb(/* only allocate space for the header now, we'll attach pages later */
				  reply_hdr_len);
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			omx_drop_dprintk(pull_eh, "PULL packet due to failure to create pull reply skb");
			err = -ENOMEM;
			goto out_with_region;
		}

		/* append segment pages */
		err = region_cache.append_pages_to_skb(&region_cache, skb, frame_length);
		if (likely(!err)) {
			/* successfully appended frags */

			/* reacquire the region and keep the reference for the destructor */
			omx_user_region_reacquire(region);
			omx_set_skb_destructor(skb, omx_send_pull_reply_skb_destructor, region);

			/* locate headers */
			reply_mh = omx_hdr(skb);
			reply_eh = &reply_mh->head.eth;

		} else {
			void *data;

			/* pages will be released in dev_kfree_skb() */
			dev_kfree_skb(skb);

 linear:
			/* failed to append, revert back to copy into a linear skb */
			omx_counter_inc(iface, PULL_REPLY_LINEAR);
			dprintk(PULL, "failed to append pages to pull reply, reverting to linear skb\n");

			/* allocate a linear skb */
			skb = omx_new_skb(/* pad to ETH_ZLEN */
					  max_t(unsigned long, reply_hdr_len + frame_length, ETH_ZLEN));
			if (unlikely(skb == NULL)) {
				omx_counter_inc(iface, SEND_NOMEM_SKB);
				omx_drop_dprintk(pull_eh, "PULL packet due to failure to create pull reply linear skb");
				err = -ENOMEM;
				goto out_with_region;
			}

			/* locate new headers */
			reply_mh = omx_hdr(skb);
			reply_eh = &reply_mh->head.eth;
			data = ((char*) reply_mh) + reply_hdr_len;

			/* copy from pages into the skb */
			region_cache.copy_pages_to_buf(&region_cache, data, frame_length);
		}

		/* fill ethernet header */
		memcpy(reply_eh->h_source, ifp->dev_addr, sizeof (reply_eh->h_source));
		reply_eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
		/* get the destination address */
		memcpy(reply_eh->h_dest, pull_eh->h_source, sizeof(reply_eh->h_dest));

		/* fill omx header */
		pull_reply_n = &reply_mh->body.pull_reply;
		OMX_PKT_FIELD_FROM(pull_reply_n->msg_offset, current_msg_offset);
		OMX_PKT_FIELD_FROM(pull_reply_n->frame_seqnum, current_frame_seqnum);
		OMX_PKT_FIELD_FROM(pull_reply_n->frame_length, frame_length);
		OMX_PKT_FIELD_FROM(pull_reply_n->ptype, OMX_PKT_TYPE_PULL_REPLY);
		OMX_PKT_FIELD_FROM(pull_reply_n->dst_pull_handle, src_pull_handle);
		OMX_PKT_FIELD_FROM(pull_reply_n->dst_magic, src_magic);

		omx_send_dprintk(reply_eh, "PULL REPLY #%d handle %ld magic %ld frame seqnum %ld length %ld offset %ld", i,
				 (unsigned long) src_pull_handle,
				 (unsigned long) src_magic,
				 (unsigned long) current_frame_seqnum,
				 (unsigned long) frame_length,
				 (unsigned long) current_msg_offset);

		omx_queue_xmit(iface, skb, PULL_REPLY);

		/* update fields now */
		current_frame_seqnum++;
		current_msg_offset += frame_length;
		block_remaining_length -= frame_length;
	}

	/* release the main reference on the region */
	omx_user_region_release(region);

	omx_endpoint_release(endpoint);
	return 0;

 out_with_region:
	/* release the main reference on the region */
	omx_user_region_release(region);
 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

/********************
 * Recv pull replies
 */

/*
 * Request more replies if necessary.
 *
 * Called on a acquired and locked handle. Unlocks it before sending and returning.
 */
static INLINE void
omx_progress_pull_on_recv_pull_reply_locked(struct omx_iface * iface,
					    struct omx_pull_handle * handle,
					    int frame_from_second_block)
{
	/* tell the sparse checker that the lock has been taken by the caller */
	__acquire(&handle->lock);

	if (!OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle)) {
		/*
		 * current first block not done, we basically just need to release the handle
		 */

		struct sk_buff *skb = NULL;

		if (frame_from_second_block
		    && OMX_PULL_HANDLE_SECOND_BLOCK_DONE(handle)
		    && !handle->already_requeued_first) {

			/* the second block is done without the first one,
			 * we assume some packet got lost in the first one,
			 * so we request the first one again
			 */

			omx_counter_inc(iface, PULL_SECOND_BLOCK_DONE_EARLY);

			dprintk(PULL, "pull handle %p second block done without first, requesting first block again\n",
				handle);

			skb = omx_fill_pull_block_request(handle, &handle->first_desc);
			if (unlikely(IS_ERR(skb))) {
				BUG_ON(PTR_ERR(skb) != -ENOMEM);
				skb = NULL;
			} else
				handle->already_requeued_first = 1;
		}

		dprintk(PULL, "block not done, just releasing\n");

		/* reschedule the timeout handler now that we are ready to send the request */
		mod_timer(&handle->retransmit_timer,
			  jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

		/* do not keep the lock while sending
		 * since the loopback device may cause reentrancy
		 */
		spin_unlock(&handle->lock);

		if (likely(skb))
			omx_queue_xmit(iface, skb, PULL_REQ);

	} else if (!OMX_PULL_HANDLE_ALL_BLOCKS_DONE(handle)) {
		/*
		 * current first block request is done
		 */

		struct sk_buff * skb = NULL, * skb2 = NULL;
		uint32_t block_length;

		omx_pull_handle_first_block_done(handle);

		if (!handle->remaining_length)
			goto skbs_ready;

		/* start the next block */
		dprintk(PULL, "queueing next pull block request\n");
		block_length = OMX_PULL_BLOCK_LENGTH_MAX;
		if (block_length > handle->remaining_length)
			block_length = handle->remaining_length;
		handle->remaining_length -= block_length;

		handle->second_desc.frame_index = handle->next_frame_index;
		handle->second_desc.block_length = block_length;
		handle->second_desc.first_frame_offset = 0;
		skb = omx_fill_pull_block_request(handle, &handle->second_desc);
		if (unlikely(IS_ERR(skb))) {
			BUG_ON(PTR_ERR(skb) != -ENOMEM);
			/* let the timeout expire and resend */
			skb = NULL;
			goto skbs_ready;
		}

		omx_pull_handle_append_needed_frames(handle, block_length, 0);

		/* the second current block (now first) request might be done too
		 * (in case of out-or-order packets)
		 */
		if (!OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle))
			goto skbs_ready;

		/* current second block request is done */
		omx_pull_handle_first_block_done(handle);

		/* is there more to request? if so, use the now-freed second block */
		if (!handle->remaining_length)
			goto skbs_ready;

		omx_counter_inc(iface, PULL_REQUEST_BOTH_BLOCKS);

		/* start another next block */
		dprintk(PULL, "queueing another next pull block request\n");
		block_length = OMX_PULL_BLOCK_LENGTH_MAX;
		if (block_length > handle->remaining_length)
			block_length = handle->remaining_length;
		handle->remaining_length -= block_length;

		handle->second_desc.frame_index = handle->next_frame_index;
		handle->second_desc.block_length = block_length;
		handle->second_desc.first_frame_offset = 0;
		skb2 = omx_fill_pull_block_request(handle, &handle->second_desc);
		if (unlikely(IS_ERR(skb2))) {
			BUG_ON(PTR_ERR(skb2) != -ENOMEM);
			/* let the timeout expire and resend */
			skb2 = NULL;
			goto skbs_ready;
		}

		omx_pull_handle_append_needed_frames(handle, block_length, 0);

	skbs_ready:
		/* reschedule the timeout handler now that we are ready to send the requests */
		mod_timer(&handle->retransmit_timer,
			  jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

		/*
		 * do not keep the lock while sending
		 * since the loopback device may cause reentrancy
		 */
		spin_unlock(&handle->lock);

		if (likely(skb))
			omx_queue_xmit(iface, skb, PULL_REQ);
		if (likely(skb2))
			omx_queue_xmit(iface, skb2, PULL_REQ);

	} else {
		/*
		 * last block is done
		 */
		omx_pull_handle_first_block_done(handle);

		spin_unlock(&handle->lock);
	}
}

int
omx_recv_pull_reply(struct omx_iface * iface,
		    struct omx_hdr * mh,
		    struct sk_buff * skb)
{
	struct omx_pkt_pull_reply *pull_reply_n = &mh->body.pull_reply;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	uint32_t dst_pull_handle = OMX_FROM_PKT_FIELD(pull_reply_n->dst_pull_handle);
	uint32_t dst_magic = OMX_FROM_PKT_FIELD(pull_reply_n->dst_magic);
	uint32_t frame_length = OMX_FROM_PKT_FIELD(pull_reply_n->frame_length);
	uint32_t frame_seqnum = OMX_FROM_PKT_FIELD(pull_reply_n->frame_seqnum);
	uint32_t msg_offset = OMX_FROM_PKT_FIELD(pull_reply_n->msg_offset);
	uint32_t frame_seqnum_offset; /* unsigned to make seqnum offset easy to check */
	struct omx_endpoint * endpoint;
	struct omx_pull_handle * handle;
	omx_frame_bitmask_t bitmap_mask;
	int err = 0;

	omx_counter_inc(iface, RECV_PULL_REPLY);

	omx_recv_dprintk(&mh->head.eth, "PULL REPLY handle %ld magic %ld frame seqnum %ld length %ld skb length %ld",
			 (unsigned long) dst_pull_handle,
			 (unsigned long) dst_magic,
			 (unsigned long) frame_seqnum,
			 (unsigned long) frame_length,
			 (unsigned long) skb->len - hdr_len);

	/* check actual data length */
	if (unlikely(frame_length > skb->len - hdr_len)) {
		omx_counter_inc(iface, DROP_BAD_SKBLEN);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) frame_length);
		err = -EINVAL;
		goto out;
	}

	/* acquire the endpoint */
	endpoint = omx_endpoint_acquire_by_pull_magic(iface, dst_magic);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with bad endpoint index within magic %ld",
				 (unsigned long) dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out;
	}
	read_lock_bh(&endpoint->pull_handles_lock);

	/* acquire the handle within the endpoint idr */
	handle = idr_find(&endpoint->pull_handles_idr, dst_pull_handle);
	if (unlikely(!handle)) {
		read_unlock_bh(&endpoint->pull_handles_lock);
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_WIRE_HANDLE);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with bad wire handle %ld",
				 (unsigned long) dst_pull_handle);
		/* no need to nack this */
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* check the full magic, if it's not fully equal, it could be
	 * the same idr from another generation of pull handle.
	 */
	if (unlikely(handle->magic != dst_magic)) {
		read_unlock_bh(&endpoint->pull_handles_lock);
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC_HANDLE_GENERATION);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with bad handle generation within magic %ld",
				 (unsigned long) dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* take a reference on the handle */
	omx_pull_handle_acquire(handle);
	read_unlock_bh(&endpoint->pull_handles_lock);

	/* no session to check */

	/* lock the handle */
	spin_lock(&handle->lock);

	/* check the status now that we own the lock */
	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		/* the handle is being closed, forget about this packet */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	/* check that the frame is from this block, and handle wrap around 256 */
	frame_seqnum_offset = (frame_seqnum - handle->frame_index + 256) % 256;
	if (unlikely(frame_seqnum_offset >= handle->block_frames)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_SEQNUM);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with invalid seqnum %ld (offset %ld), should be within %ld-%ld",
				 (unsigned long) frame_seqnum,
				 (unsigned long) frame_seqnum_offset,
				 (unsigned long) handle->frame_index,
				 (unsigned long) handle->frame_index + handle->block_frames);
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	/* check that the frame is not a duplicate */
	bitmap_mask = ((omx_frame_bitmask_t)1) << frame_seqnum_offset;
	if (unlikely((handle->frames_missing_bitmap & bitmap_mask) == 0)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_DUPLICATE);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with duplicate seqnum %ld (offset %ld) in current block %ld-%ld",
				 (unsigned long) frame_seqnum,
				 (unsigned long) frame_seqnum_offset,
				 (unsigned long) handle->frame_index,
				 (unsigned long) handle->frame_index + handle->block_frames);
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}
	handle->frames_missing_bitmap &= ~bitmap_mask;

	/* our copy is pending */
	handle->frames_copying_nr++;

	/* request more replies if necessary */
	omx_progress_pull_on_recv_pull_reply_locked(iface, handle,
						    frame_seqnum_offset >= OMX_PULL_REPLY_PER_BLOCK);
	/* tell the sparse checker that the lock has been released by omx_progress_pull_on_recv_pull_reply_locked() */
	__release(&handle->lock);

	/* fill segment pages */
	dprintk(PULL, "copying PULL_REPLY %ld bytes for msg_offset %ld at region offset %ld\n",
	       (unsigned long) frame_length,
	       (unsigned long) msg_offset,
	       (unsigned long) msg_offset + handle->puller_rdma_offset);
	err = omx_user_region_fill_pages(handle->region,
					 msg_offset + handle->puller_rdma_offset,
					 skb,
					 frame_length);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, PULL_REPLY_FILL_FAILED);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet due to failure to fill pages from skb");
		/* the other peer is sending crap, close the handle and report truncated to userspace
		 * we do not really care about what have been tranfered since it's crap
		 */
		spin_lock(&handle->lock);
		omx_pull_handle_done_notify(handle, OMX_EVT_PULL_DONE_ABORTED);
		omx_pull_handle_done_release(handle);
		/* tell the sparse checker that the lock has been released by omx_pull_handle_done_release() */
		__release(&handle->lock);
		goto out_with_endpoint;
	}

	/* take the lock back to prepare to complete */
	spin_lock(&handle->lock);

	/* our copy is done */
	handle->frames_copying_nr--;

	/* check the status now that we own the lock */
	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		/* the handle is being closed, forget about this packet */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	if (OMX_PULL_HANDLE_ALL_BLOCKS_DONE(handle)
	    && handle->frames_copying_nr == 0) {
		/* notify the completion */
		dprintk(PULL, "notifying pull completion\n");
		omx_pull_handle_done_notify(handle, OMX_EVT_PULL_DONE_SUCCESS);
		omx_pull_handle_done_release(handle);
		/* tell the sparse checker that the lock has been released by omx_pull_handle_done_release() */
		__release(&handle->lock);
	} else {
		/* there's more to receive or copy, just release the handle */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

/******************
 * Recv pull nacks
 */

int
omx_recv_nack_mcp(struct omx_iface * iface,
		  struct omx_hdr * mh,
		  struct sk_buff * skb)
{
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_nack_mcp *nack_mcp_n = &mh->body.nack_mcp;
	enum omx_nack_type nack_type = OMX_FROM_PKT_FIELD(nack_mcp_n->nack_type);
	uint32_t dst_pull_handle = OMX_FROM_PKT_FIELD(nack_mcp_n->src_pull_handle);
	uint32_t dst_magic = OMX_FROM_PKT_FIELD(nack_mcp_n->src_magic);
	struct omx_endpoint * endpoint;
	struct omx_pull_handle * handle;
	int err = 0;

	omx_counter_inc(iface, RECV_NACK_MCP);

	omx_recv_dprintk(eh, "NACK MCP type %s",
			 omx_strnacktype(nack_type));

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		/* FIXME: impossible? in non MX-wire compatible only? */
		uint32_t src_addr_peer_index;
		uint64_t src_addr;

		if (peer_index != (uint16_t)-1) {
			omx_drop_dprintk(eh, "NACK MCP with bad peer index %d",
					 (unsigned) peer_index);
			goto out;
		}

		src_addr = omx_board_addr_from_ethhdr_src(eh);
		err = omx_peer_lookup_by_addr(src_addr, NULL, &src_addr_peer_index);
		if (err < 0) {
			omx_drop_dprintk(eh, "NACK MCP with unknown peer index and unknown address");
			goto out;
		}

		peer_index = src_addr_peer_index;
	}

	/* acquire the endpoint */
	endpoint = omx_endpoint_acquire_by_pull_magic(iface, dst_magic);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "NACK MCP packet with bad endpoint index within magic %ld",
				 (unsigned long) dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out;
	}
	read_lock_bh(&endpoint->pull_handles_lock);

	/* acquire the handle within the endpoint idr */
	handle = idr_find(&endpoint->pull_handles_idr, dst_pull_handle);
	if (unlikely(!handle)) {
		read_unlock_bh(&endpoint->pull_handles_lock);
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_WIRE_HANDLE);
		omx_drop_dprintk(&mh->head.eth, "NACK MCP packet with bad wire handle %ld",
				 (unsigned long) dst_pull_handle);
		/* no need to nack this */
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* check the full magic, if it's not fully equal, it could be
	 * the same idr from another generation of pull handle.
	 */
	if (unlikely(handle->magic != dst_magic)) {
		read_unlock_bh(&endpoint->pull_handles_lock);
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC_HANDLE_GENERATION);
		omx_drop_dprintk(&mh->head.eth, "NACK MCP packet with bad handle generation within magic %ld",
				 (unsigned long) dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* take a reference on the handle and release the endpoint since the handle owns it */
	omx_pull_handle_acquire(handle);
	read_unlock_bh(&endpoint->pull_handles_lock);

	/* no session to check */

	/* lock the handle and complete it */
	spin_lock(&handle->lock);

	/* check the status now that we own the lock */
	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		/* the handle is being closed, forget about this packet */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	/* complete the handle */
	omx_pull_handle_done_notify(handle, nack_type);
	omx_pull_handle_done_release(handle);
	/* tell the sparse checker that the lock has been released by omx_pull_handle_done_release() */
	__release(&handle->lock);
	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
