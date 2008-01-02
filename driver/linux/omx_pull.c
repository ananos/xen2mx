/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
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
#include "omx_endpoint.h"
#include "omx_region.h"

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
	 */
	OMX_PULL_HANDLE_STATUS_OK,

	/*
	 * The handle has been removed from the idr and the endpoint list, but the timeout handler is still running.
	 * Either the pull has completed (or aborted on error), or the endpoint is being closed and
	 * all handles have been scheduled for removal.
	 * The timeout handler must exit next time it runs. It will release the reference on the handle
	 * so that the handle may be destroyed.
	 */
	OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT,

	/*
	 * The handle has been removed from the idr and the endpoint list, and its timeout handler has exited
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
	struct list_head list_elt;
	uint32_t idr_index;

	/* timer for retransmission */
	struct timer_list retransmit_timer;
	uint64_t last_retransmit_jiffies;

	/* global pull fields */
	struct omx_endpoint * endpoint;
	struct omx_user_region * region;
	uint32_t lib_cookie;
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
	omx_frame_bitmask_t frame_missing_bitmap; /* frames not received at all */
	omx_frame_bitmask_t frame_copying_bitmap; /* frames received but not copied yet */
	struct omx_pull_block_desc first_desc;
	struct omx_pull_block_desc second_desc;
	uint32_t already_requeued_first; /* the first block has been requested again since the last timer */

	/* pull packet header */
	struct omx_hdr pkt_hdr;
};

static void omx_pull_handle_timeout_handler(unsigned long data);
static void omx_pull_handle_done_notify(struct omx_pull_handle * handle, uint8_t status);

/*
 * Notes about locking:
 *
 * Each handle owns a reference on the endpoint during its whole life.
 * It is released when the last handle reference is released. One is used
 * as long as the timeout handler is pending. The other one is taken while
 * processing incoming pull replies.
 *
 * Each handle also owns a spinlock to protect in variable internal state.
 * It status field describes whether it is being processed or not, and
 * whether its timer is still active. Once both are done, the handle may
 * be cleaned.
 *
 * The pile of handles for an endpoint is protected by a rwlock. It is taken
 * for reading when acquiring an handle (when a pull reply arrives, likely
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

#ifdef OMX_DEBUG
/* defined as module parameters */
extern unsigned long omx_PULL_packet_loss;
extern unsigned long omx_PULL_REPLY_packet_loss;
/* index between 0 and the above limit */
static unsigned long omx_PULL_packet_loss_index = 0;
static unsigned long omx_PULL_REPLY_packet_loss_index = 0;
#endif

/********************************
 * Pull handle bitmap management
 */

#if OMX_PULL_REPLY_PER_BLOCK >= 32
#error Cannot request more than 32 replies per pull block
#endif

#define OMX_PULL_HANDLE_BLOCK_BITMASK ((((omx_frame_bitmask_t)1)<<OMX_PULL_REPLY_PER_BLOCK)-1)
#define OMX_PULL_HANDLE_SECOND_BLOCK_BITMASK (OMX_PULL_HANDLE_BLOCK_BITMASK<<OMX_PULL_REPLY_PER_BLOCK)
#define OMX_PULL_HANDLE_BOTH_BLOCKS_BITMASK ((((omx_frame_bitmask_t)1)<<(2*OMX_PULL_REPLY_PER_BLOCK))-1)

#define OMX_PULL_HANDLE_DONE(handle) \
	(!((handle)->remaining_length) \
	 && !((handle)->frame_copying_bitmap & OMX_PULL_HANDLE_BOTH_BLOCKS_BITMASK))

#define OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle) \
	(!((handle)->frame_copying_bitmap & OMX_PULL_HANDLE_BLOCK_BITMASK))

#define OMX_PULL_HANDLE_SECOND_BLOCK_DONE(handle) \
	(!((handle)->frame_copying_bitmap & OMX_PULL_HANDLE_SECOND_BLOCK_BITMASK))

static inline void
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

	handle->frame_missing_bitmap |= new_mask;
	handle->frame_copying_bitmap |= new_mask;
	handle->block_frames += new_frames;
	handle->next_frame_index += new_frames;
}

static inline void
omx_pull_handle_first_block_done(struct omx_pull_handle * handle)
{
	uint32_t first_block_frames = handle->block_frames > OMX_PULL_REPLY_PER_BLOCK
	 ? handle->block_frames - OMX_PULL_REPLY_PER_BLOCK : handle->block_frames;

	handle->frame_missing_bitmap >>= first_block_frames;
	handle->frame_copying_bitmap >>= first_block_frames;
	handle->frame_index += first_block_frames;
	handle->block_frames -= first_block_frames;
	memcpy(&handle->first_desc, &handle->second_desc,
	       sizeof(struct omx_pull_block_desc));
	handle->already_requeued_first = 0;
}

/******************
 * Global pull idr
 */

static struct idr omx_pull_handles_idr;
static rwlock_t omx_pull_handles_idr_lock;

int
omx_pull_handles_init(void)
{
	idr_init(&omx_pull_handles_idr);
	rwlock_init(&omx_pull_handles_idr_lock);
	return 0;
}

void
omx_pull_handles_exit(void)
{
	idr_destroy(&omx_pull_handles_idr);
}

/***************************************
 * Per-endpoint pull handles management
 */

int
omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint)
{
	INIT_LIST_HEAD(&endpoint->pull_handles_list);
	rwlock_init(&endpoint->pull_handles_list_lock);
	return 0;
}

static void
__omx_pull_handle_last_release(struct kref * kref)
{
	struct omx_pull_handle * handle = container_of(kref, struct omx_pull_handle, refcount);
	struct omx_endpoint * endpoint = handle->endpoint;

	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_EXITED);
	kfree(handle);

	/* we don't depend on the endpoint anymore now, release the endpoint reference */
	omx_endpoint_release(endpoint);
}

void
omx_endpoint_pull_handles_prepare_exit(struct omx_endpoint * endpoint)
{
	struct omx_pull_handle * handle, * next;

	/* ask all pull handles of the endpoint to stop their timer */
	write_lock_bh(&endpoint->pull_handles_list_lock);

	list_for_each_entry_safe(handle, next,
				 &endpoint->pull_handles_list,
				 list_elt) {

		BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);
		handle->status = OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT;

		/* remove from the idr so that no incoming packet can find it anymore */
		write_lock(&omx_pull_handles_idr_lock);
		idr_remove(&omx_pull_handles_idr, handle->idr_index);
		write_unlock(&omx_pull_handles_idr_lock);

		list_del(&handle->list_elt);
	}

	write_unlock_bh(&endpoint->pull_handles_list_lock);
}

/******************************
 * Endpoint pull-magic management
 */

#define OMX_ENDPOINT_PULL_MAGIC_XOR 0x22111867
#define OMX_ENDPOINT_PULL_MAGIC_SHIFT 13

static inline uint32_t
omx_endpoint_pull_magic(struct omx_endpoint * endpoint)
{
	uint32_t magic;

	magic = (((uint32_t)endpoint->endpoint_index) << OMX_ENDPOINT_PULL_MAGIC_SHIFT)
		^ OMX_ENDPOINT_PULL_MAGIC_XOR;

	return magic;
}

/******************************
 * Per-endpoint pull handles create/find/...
 */

static inline int
omx_pull_handle_pkt_hdr_fill(struct omx_endpoint * endpoint,
			     struct omx_pull_handle * handle,
			     struct omx_cmd_send_pull * cmd)
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
	OMX_PKT_FIELD_FROM(pull_n->src_magic, omx_endpoint_pull_magic(endpoint));

	/* block_length, frame_index, and first_frame_offset filled at actual send */

	return 0;

 out:
	return ret;
}

/*
 * Create a pull handle and return it as acquired and locked.
 */
static inline struct omx_pull_handle *
omx_pull_handle_create(struct omx_endpoint * endpoint,
		       struct omx_cmd_send_pull * cmd)
{
	struct omx_pull_handle * handle;
	struct omx_user_region * region;
	int err;

	/* take another reference and keep it during the handle life */
	omx_endpoint_reacquire(endpoint);

	/* acquire the region */
	region = omx_user_region_acquire(endpoint, cmd->local_rdma_id);
	if (unlikely(!region)) {
		err = -ENOMEM;
		goto out_with_endpoint;
	}

	/* alloc the pull handle */
	handle = kmalloc(sizeof(struct omx_pull_handle), GFP_KERNEL);
	if (unlikely(!handle)) {
		printk(KERN_INFO "Open-MX: Failed to allocate a pull handle\n");
		goto out_with_region;
	}

	/* while failed, realloc and retry */
 idr_try_alloc:
	err = idr_pre_get(&omx_pull_handles_idr, GFP_KERNEL);
	if (unlikely(!err)) {
		printk(KERN_ERR "Open-MX: Failed to allocate idr space for pull handles\n");
		err = -ENOMEM;
		goto out_with_handle;
	}

	write_lock_bh(&omx_pull_handles_idr_lock);
	err = idr_get_new(&omx_pull_handles_idr, handle, &handle->idr_index);
	if (unlikely(err == -EAGAIN)) {
		write_unlock_bh(&omx_pull_handles_idr_lock);
		printk("omx_pull_handle_create try again\n");
		goto idr_try_alloc;
	}
	write_unlock_bh(&omx_pull_handles_idr_lock);

	/* we are good now, finish filling the handle */
	kref_init(&handle->refcount);
	handle->endpoint = endpoint;
	handle->region = region;

	/* fill handle */
	handle->lib_cookie = cmd->lib_cookie;
	handle->total_length = cmd->length;
	handle->puller_rdma_offset = cmd->local_offset;
	handle->pulled_rdma_offset = cmd->remote_offset;

	/* initialize variable stuff */
	spin_lock_init(&handle->lock);
	spin_lock(&handle->lock);
	handle->status = OMX_PULL_HANDLE_STATUS_OK;
	handle->remaining_length = cmd->length;
	handle->frame_index = 0;
	handle->next_frame_index = 0;
	handle->block_frames = 0;
	handle->frame_missing_bitmap = 0;
	handle->frame_copying_bitmap = 0;
	handle->already_requeued_first = 0;
	handle->last_retransmit_jiffies = cmd->retransmit_delay_jiffies + jiffies;

	/* initialize cached header */
	err = omx_pull_handle_pkt_hdr_fill(endpoint, handle, cmd);
	if (err < 0)
		goto out_with_idr;

	/* init timer */
	setup_timer(&handle->retransmit_timer, omx_pull_handle_timeout_handler,
		    (unsigned long) handle);
	__mod_timer(&handle->retransmit_timer,
		    jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	/* queue in the endpoint list */
	write_lock_bh(&endpoint->pull_handles_list_lock);
	list_add_tail(&handle->list_elt,
		      &endpoint->pull_handles_list);
	write_unlock_bh(&endpoint->pull_handles_list_lock);

	dprintk(PULL, "created and acquired pull handle %p\n", handle);
	return handle;

 out_with_idr:
	write_lock_bh(&omx_pull_handles_idr_lock);
	idr_remove(&omx_pull_handles_idr, handle->idr_index);
	write_unlock_bh(&omx_pull_handles_idr_lock);
 out_with_handle:
	kfree(handle);
 out_with_region:
	omx_user_region_release(region);
 out_with_endpoint:
	omx_endpoint_release(endpoint);
	return NULL;
}

/*
 * Acquire a pull handle and the corresponding endpoint
 * given by an pull magic and a wire handle.
 *
 * May be called by the bottom half.
 */
static inline struct omx_pull_handle *
omx_pull_handle_acquire_by_wire(struct omx_iface * iface,
				uint32_t magic, uint32_t wire_handle)
{
	struct omx_pull_handle * handle;
	struct omx_endpoint * endpoint;

	read_lock_bh(&omx_pull_handles_idr_lock);
	handle = idr_find(&omx_pull_handles_idr, wire_handle);
	if (!handle)
		goto out_with_idr_lock;

	endpoint = handle->endpoint;
	if (magic != omx_endpoint_pull_magic(endpoint))
		goto out;

	kref_get(&handle->refcount);

	read_unlock_bh(&omx_pull_handles_idr_lock);

	dprintk(PULL, "acquired pull handle %p\n", handle);
	return handle;

 out_with_idr_lock:
	read_unlock_bh(&omx_pull_handles_idr_lock);
 out:
	return NULL;
}

/* Release an acquire pull handle */
static void
omx_pull_handle_release(struct omx_pull_handle * handle)
{
	kref_put(&handle->refcount, __omx_pull_handle_last_release);
}	

/*
 * Takes an acquired and locked pull handle and complete it.
 *
 * May be called by the BH after receiving a pull reply or a nack,
 * by the retransmit timer when expired, or with in an ioctl if the
 * posting of a pull failed.
 */
static inline void
__omx_pull_handle_done_release(struct omx_pull_handle * handle)
{
	struct omx_user_region * region = handle->region;
	struct omx_endpoint * endpoint = handle->endpoint;

	spin_unlock(&handle->lock);

	/* remove from the idr (and endpoint list) so that no incoming packet can find it anymore */
	write_lock_bh(&omx_pull_handles_idr_lock);
	idr_remove(&omx_pull_handles_idr, handle->idr_index);
	write_unlock_bh(&omx_pull_handles_idr_lock);

	write_lock_bh(&endpoint->pull_handles_list_lock);
	list_del(&handle->list_elt);
	write_unlock_bh(&endpoint->pull_handles_list_lock);

	/* release the region and handle */
	omx_user_region_release(region);
	omx_pull_handle_release(handle);
}

/* __omx_pull_handle_done_release called by the bottom half or ioctl */
static inline void
omx_pull_handle_done_release(struct omx_pull_handle * handle)
{
	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);
	handle->status = OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT;
	__omx_pull_handle_done_release(handle);
}

/* __omx_pull_handle_done_release called by the timer */
static inline void
omx_pull_handle_timer_release(struct omx_pull_handle * handle)
{
	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);
	handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;
	__omx_pull_handle_done_release(handle);
}

/******************************
 * Pull-related networking
 */

/* Called with the handle acquired and locked */
static inline struct sk_buff *
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

	/* start the timer */
	mod_timer(&handle->retransmit_timer,
		  jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	return skb;
}

int
omx_send_pull(struct omx_endpoint * endpoint,
	      void __user * uparam)
{
	struct omx_cmd_send_pull cmd;
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

	/* create, acquire and lock the handle */
	handle = omx_pull_handle_create(endpoint, &cmd);
	if (unlikely(!handle)) {
		printk(KERN_INFO "Open-MX: Failed to allocate a pull handle\n");
		err = -ENOMEM;
		goto out;
	}

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
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto out_with_handle;
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
	if (IS_ERR(skb2)) {
		err = PTR_ERR(skb2);
		dev_kfree_skb(skb);
		goto out_with_handle;
	}

	omx_pull_handle_append_needed_frames(handle, block_length, 0);

 skbs_ready:
	spin_unlock(&handle->lock);

	omx_queue_xmit(iface, skb, PULL);
	if (skb2)
		omx_queue_xmit(iface, skb2, PULL);

	return 0;

 out_with_handle:
	/* we failed to send the first pull requests,
	 * report an error to the user right now
	 */
	omx_pull_handle_done_release(handle);
 out:
	return err;
}

/* retransmission callback, owns a reference on the endpoint */
static void omx_pull_handle_timeout_handler(unsigned long data)
{
	struct omx_pull_handle * handle = (void *) data;
	struct omx_endpoint * endpoint = handle->endpoint;
	struct omx_iface * iface = endpoint->iface;
	struct sk_buff * skb;

	dprintk(PULL, "pull handle %p timer reached, might need to request again\n", handle);

	spin_lock(&handle->lock);

	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT);
		handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		return; /* timer will never be called again (status is TIMER_EXITED) */
	}

	if (jiffies > handle->last_retransmit_jiffies) {
		omx_counter_inc(iface, PULL_TIMEOUT_ABORT);
		dprintk(PULL, "pull handle last retransmit time reached, reporting an error\n");
		omx_pull_handle_done_notify(handle, OMX_EVT_PULL_DONE_TIMEOUT);
		omx_pull_handle_timer_release(handle);
		return; /* timer will never be called again (status is TIMER_EXITED) */
	}

	BUG_ON(OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle));

	/* request the first block again */
	omx_counter_inc(iface, PULL_TIMEOUT_HANDLER_FIRST_BLOCK);

	skb = omx_fill_pull_block_request(handle, &handle->first_desc);
	if (!IS_ERR(skb))
		omx_queue_xmit(iface, skb, PULL);

	handle->already_requeued_first = 0;

	/*
	 * If the second block isn't done either, request it again
	 * (otherwise the 2-block pipeline would be broken for ever)
	 * This shouldn't happen often since it means a packet has been lost
	 * in both first and second blocks.
	 */
	if (!OMX_PULL_HANDLE_SECOND_BLOCK_DONE(handle)) {
		omx_counter_inc(iface, PULL_TIMEOUT_HANDLER_SECOND_BLOCK);

		skb = omx_fill_pull_block_request(handle, &handle->second_desc);
		if (!IS_ERR(skb))
			omx_queue_xmit(iface, skb, PULL);
	}

	mod_timer(&handle->retransmit_timer,
		  jiffies + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	spin_unlock(&handle->lock);
}

/* pull reply skb destructor to release the user region */
static void
omx_send_pull_reply_skb_destructor(struct sk_buff *skb)
{
	struct omx_user_region * region = (void *) skb->sk;

	omx_user_region_release(region);
}

int
omx_recv_pull(struct omx_iface * iface,
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
	struct omx_pkt_pull_reply *pull_reply_n;
	struct omx_hdr *reply_mh;
	struct ethhdr *reply_eh;
	size_t reply_hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	struct omx_user_region *region;
	struct sk_buff *skb = NULL;
	uint32_t current_frame_seqnum, current_msg_offset, block_remaining_length;
	int replies, i;
	int err = 0;

	omx_counter_inc(iface, RECV_PULL);

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

	/* prepare all skbs now */
	for(i=0; i<replies; i++) {
		uint32_t frame_length;

		/* allocate a skb */
		struct sk_buff *skb = omx_new_skb(/* only allocate space for the header now,
						   * we'll attach pages and pad to ETH_ZLEN later
						   */
						  reply_hdr_len);
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			omx_drop_dprintk(pull_eh, "PULL packet due to failure to create pull reply skb");
			err = -ENOMEM;
			goto out_with_region_once;
		}

		/* locate headers */
		reply_mh = omx_hdr(skb);
		reply_eh = &reply_mh->head.eth;

		/* fill ethernet header */
		memcpy(reply_eh->h_source, ifp->dev_addr, sizeof (reply_eh->h_source));
		reply_eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
		/* get the destination address */
		memcpy(reply_eh->h_dest, pull_eh->h_source, sizeof(reply_eh->h_dest));

		frame_length = (i==0) ? OMX_PULL_REPLY_LENGTH_MAX - first_frame_offset
			: OMX_PULL_REPLY_LENGTH_MAX;
		if (block_remaining_length < frame_length)
			frame_length = block_remaining_length;

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

		/* reacquire the rdma window once per reply */
		omx_user_region_reacquire(region);

		/* append segment pages */
		err = omx_user_region_append_pages(region, current_msg_offset + pulled_rdma_offset,
						   skb, frame_length);
		if (unlikely(err < 0)) {
			omx_counter_inc(iface, PULL_REPLY_APPEND_FAIL);
			omx_drop_dprintk(pull_eh, "PULL packet due to failure to append pages to skb");
			/* pages will be released in dev_kfree_skb() */
			goto out_with_skb_and_region_twice;
		}

		if (unlikely(skb->len < ETH_ZLEN)) {
			/* pad to ETH_ZLEN */
			err = omx_skb_pad(skb, ETH_ZLEN);
			if (unlikely(err < 0)) {
				/* skb has already been freed in skb_pad() */
				goto out_with_region_twice;
			}
			skb->len = ETH_ZLEN;
		}

		skb->sk = (void *) region;
		skb->destructor = omx_send_pull_reply_skb_destructor;

		/* now that the skb is ready, remove it from the array
		 * so that we don't try to free it in case of error later
		 */
		omx_queue_xmit(iface, skb, PULL_REPLY);

		/* update fields now */
		current_frame_seqnum++;
		current_msg_offset += frame_length;
		block_remaining_length -= frame_length;
	}

	/* release the main hold on the region */
	omx_user_region_release(region);

	omx_endpoint_release(endpoint);
	return 0;

 out_with_skb_and_region_twice:
	dev_kfree_skb(skb);
 out_with_region_twice:
	omx_user_region_release(region);
 out_with_region_once:
	omx_user_region_release(region);
 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
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

	/* make sure the handle will be released, in case we are reporting truncation */
	handle->frame_missing_bitmap = 0;
	handle->frame_copying_bitmap = 0;
	handle->remaining_length = 0;
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
	struct omx_pull_handle * handle;
	omx_frame_bitmask_t bitmap_mask;
	int frame_from_second_block = 0;
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

	/* acquire the handle and endpoint */
	handle = omx_pull_handle_acquire_by_wire(iface, dst_magic, dst_pull_handle);
	if (unlikely(!handle)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet unknown handle %d magic %d",
				 dst_pull_handle, dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out;
	}

	/* no session to check */

	/* lock the handle */
	spin_lock(&handle->lock);

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
		goto out;
	}

	/* check that the frame is not a duplicate */
	bitmap_mask = ((omx_frame_bitmask_t)1) << frame_seqnum_offset;
	if (unlikely((handle->frame_missing_bitmap & bitmap_mask) == 0)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_DUPLICATE);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with duplicate seqnum %ld (offset %ld) in current block %ld-%ld",
				 (unsigned long) frame_seqnum,
				 (unsigned long) frame_seqnum_offset,
				 (unsigned long) handle->frame_index,
				 (unsigned long) handle->frame_index + handle->block_frames);
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out;
	}
	handle->frame_missing_bitmap &= ~bitmap_mask;

	/* release the lock during the copy */
	spin_unlock(&handle->lock);

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
		goto out;
	}

	/* take the lock back to prepare the future */
	spin_lock(&handle->lock);

	/*
	 * handle->frame_index may have changed while the lock was released if we are
	 * processing a frame of the second block and one from the first block has been
	 * processed in the meantime. Reupdate our offset.
	 */
	frame_seqnum_offset = (frame_seqnum - handle->frame_index + 256) % 256;
	bitmap_mask = ((omx_frame_bitmask_t)1) << frame_seqnum_offset;
	handle->frame_copying_bitmap &= ~bitmap_mask;

	if (frame_seqnum_offset >= OMX_PULL_REPLY_PER_BLOCK)
		frame_from_second_block = 1;

	if (!OMX_PULL_HANDLE_FIRST_BLOCK_DONE(handle)) {

		/* current first block not done, we basically just need to release the handle */

		if (frame_from_second_block
		    && OMX_PULL_HANDLE_SECOND_BLOCK_DONE(handle)
		    && !handle->already_requeued_first) {

			/* the second block is done without the first one,
			 * we assume some packet got lost in the first one,
			 * so we request the first one again
			 */

			struct sk_buff *skb;

			omx_counter_inc(iface, PULL_SECOND_BLOCK_DONE_EARLY);

			dprintk(PULL, "pull handle %p second block done without first, requesting first block again\n",
				handle);

			skb = omx_fill_pull_block_request(handle, &handle->first_desc);
			if (!IS_ERR(skb))
				omx_queue_xmit(iface, skb, PULL);

			handle->already_requeued_first = 1;
		}

		dprintk(PULL, "block not done, just releasing\n");
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);

	} else if (!OMX_PULL_HANDLE_DONE(handle)) {

		/* current first block request is done */

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
		if (IS_ERR(skb)) {
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
		if (IS_ERR(skb2)) {
			BUG_ON(PTR_ERR(skb2) != -ENOMEM);
			/* let the timeout expire and resend */
			skb2 = NULL;
			goto skbs_ready;
		}

		omx_pull_handle_append_needed_frames(handle, block_length, 0);

	skbs_ready:
		/* release the handle before sending to avoid
		 * deadlock when sending to ourself in the same region
		 */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);

		if (skb)
			omx_queue_xmit(iface, skb, PULL);
		if (skb2)
			omx_queue_xmit(iface, skb2, PULL);

	} else {

		/* last block is done */
		omx_pull_handle_first_block_done(handle);

		/* notify the completion */
		dprintk(PULL, "notifying pull completion\n");
		omx_pull_handle_done_notify(handle, OMX_EVT_PULL_DONE_SUCCESS);
		omx_pull_handle_done_release(handle);
	}

	return 0;

 out:
	return err;
}

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
	struct omx_pull_handle * handle;
	int err = 0;

	omx_counter_inc(iface, RECV_NACK_MCP);

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

	/* acquire the handle */
	handle = omx_pull_handle_acquire_by_wire(iface, dst_magic, dst_pull_handle);
	if (unlikely(!handle)) {
		omx_counter_inc(iface, DROP_NACK_MCP_BAD_MAGIC);
		omx_drop_dprintk(&mh->head.eth, "NACK MCP packet unknown handle %d magic %d",
				 dst_pull_handle, dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out;
	}

	omx_recv_dprintk(eh, "NACK MCP type %s",
			 omx_strnacktype(nack_type));

	spin_lock(&handle->lock);
	omx_pull_handle_done_notify(handle, nack_type);
	omx_pull_handle_done_release(handle);

	return 0;

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
