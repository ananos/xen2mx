/*
 * Open-MX
 * Copyright © inria 2007-2010 (see AUTHORS file)
 * Copyright © Anastassios Nanos 2012
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
#include <linux/kref.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_peer.h"
#include "omx_endpoint.h"
#include "omx_reg.h"
#include "omx_dma.h"
#include "omx_shared.h"

//#define TIMERS_ENABLED
#include "omx_xen_timers.h"
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_reg.h"
#include "omx_xen_lib.h"
/**************************
 * Pull-specific Constants
 */

timers_t t_pull_request, t_pull_reply, t_pull, t_handle;
#define OMX_PULL_RETRANSMIT_TIMEOUT_MS	1000
#define OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES (OMX_PULL_RETRANSMIT_TIMEOUT_MS*HZ/1000)

#ifdef OMX_MX_WIRE_COMPAT
#if OMX_PULL_REPLY_LENGTH_MAX >= 65536
#error Cannot store rdma offsets > 65535 in 16bits offsets on the wire
#endif
#endif

#define OMX_ENDPOINT_PULL_MAGIC_XOR 0x21071980

/**********************
 * Pull-specific Types
 */

#if OMX_PULL_REPLY_PER_BLOCK & (OMX_PULL_REPLY_PER_BLOCK-1)
/* we don't want to divide by a non-power-of-two */
#error Need a power of two as the number of replies per pull block
#elif OMX_PULL_REPLY_PER_BLOCK > 64
#error Cannot request more than 64 replies per pull block
#elif OMX_PULL_REPLY_PER_BLOCK > 32
typedef uint64_t omx_block_frame_bitmask_t;
#elif OMX_PULL_REPLY_PER_BLOCK > 16
typedef uint32_t omx_block_frame_bitmask_t;
#elif OMX_PULL_REPLY_PER_BLOCK > 8
typedef uint16_t omx_block_frame_bitmask_t;
#else
typedef uint8_t omx_block_frame_bitmask_t;
#endif

enum omx_pull_handle_status {
	/*
	 * The handle is normal, being processed as usual and its timeout handler is running ok.
	 * It is queued on the endpoint list.
	 */
	OMX_PULL_HANDLE_STATUS_OK,

	/*
	 * The handle has been removed from the slot array since no incoming packet can use it.
	 * But the timeout handler is still running, so the handle is still queued on the endpoint list.
	 * Either the pull has completed (or aborted on error), or the endpoint is being closed and
	 * the timeout handle must exit net time it runs (or del_timer_sync will kill it first).
	 */
	OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT,

	/*
	 * The handle has been removed from the slot array and the endpoint list.
	 * Its timeout handler has exited and released its reference on the handle and endpoint.
	 * Either the pull has completed (or aborted on error) and the handler caught the MUST_EXIT status,
	 * or the timeout was reached and its handler aborted the handle directly.
	 */
	OMX_PULL_HANDLE_STATUS_TIMER_EXITED,
};

struct omx_pull_block_desc {
	uint32_t frame_index;
	uint32_t block_length;
	uint32_t first_frame_offset;
	omx_block_frame_bitmask_t frames_missing_bitmap; /* frames not received at all */
};

struct omx_pull_handle {
	struct kref refcount;
	struct list_head list_elt; /* always queued on one of the endpoint lists */
	uint8_t xen:1;

	uint32_t slot_id; /* 32bits slot identifier */

	/* timer for retransmission */
	struct timer_list retransmit_timer;
	uint64_t last_retransmit_jiffies;

	/* global pull fields */
	struct omx_endpoint * endpoint;
	struct omx_user_region * region;
	struct omx_xen_user_region * xregion;
	uint32_t total_length;
	uint32_t pulled_rdma_offset;

	/* current status */
	spinlock_t lock;
	enum omx_pull_handle_status status;
	uint32_t remaining_length;
	uint32_t frame_index; /* index of the first requested frame */
	uint32_t next_frame_index; /* index of the frame to request */
	uint32_t nr_requested_frames; /* number of frames requested */
	uint32_t nr_missing_frames; /* frames requested but not received yet */
	uint32_t nr_valid_block_descs;
	uint32_t already_rerequested_blocks; /* amount of first blocks that were requested again since the last timer */
	struct omx_pull_block_desc block_desc[OMX_PULL_BLOCK_DESCS_NR];

	/* synchronous host copies */
	uint32_t host_copy_nr_frames; /* frames received but not copied yet*/

	/* asynchronous DMA engine copies */
#ifdef OMX_HAVE_DMA_ENGINE
	struct dma_chan *dma_copy_chan; /* NULL when no pending copy */
	dma_cookie_t dma_copy_last_cookie; /* -1 when no pending copy */
	struct sk_buff_head dma_copy_skb_queue; /* used without its internal lock */
	struct work_struct dma_copy_deferred_wait_work;
#endif

	/* completion event */
	struct omx_evt_pull_done done_event;

	/* pull packet header */
	struct omx_hdr pkt_hdr;
};

static void omx_pull_handle_timeout_handler(unsigned long data);

#ifdef OMX_HAVE_DMA_ENGINE
static void omx_pull_handle_poll_dma_completions(struct omx_pull_handle *handle);
static int omx_pull_handle_deferred_wait_dma_completions(struct omx_pull_handle *handle);
static void omx_pull_handle_deferred_dma_completions_wait_workfunc(omx_work_struct_data_t data);
#else
#define omx_pull_handle_poll_dma_completions(ph) /* nothing */
#define omx_pull_handle_deferred_wait_dma_completions(ph) 0 /* always completed */
#endif

/*
 * Notes about locking:
 *
 * Each handle owns a spin_lock that protects the actual pull status (frame index, ...).
 * It also protects its handle status and its queueing in the endpoint lists and slot array.
 * This lock is always taken *before* the endpoint pull handle lock.
 *
 * The handle is always queued on endpoint list as long as its timer is running.
 * As long as the timer is pending, a reference on the handle and endpoint is held.
 * The endpoint closing enforces its destruction. When the endpoint starts to be
 * closed (either ioctl, or last closing of the file descriptor, or interface being
 * removed), it calls pull_handles_exit which sets all handles to timer_must_exit
 * and uses del_timer_sync to either cancel the next timer or wait for it to end.
 * Then the timer will never run again and the handle may safely be destroyed.
 *
 * The pile of handles for an endpoint is protected by a spinlock. It is not taken
 * when acquiring an handle (when a pull reply or nack mcp arrives, likely in a
 * bottom half) because this is RCU protected. It is only taken for modification
 * when creating a handle (when the application request a pull), finishing a handle
 * (when a pull reply completes the pull request, likely in a bottom half), when
 * completing a handle on timeout (from the timer softirq), and when destroying
 * remaining handles (when the endpoint is closed).
 * Since a bottom half and the application may both acquire the spinlock, we must
 * always disable bottom halves when taking the spinlock.
 */

/*
 * Notes about retransmission:
 *
 * The puller requests OMX_PULL_BLOCK_DESCS_NR blocks of data, and waits for
 * OMX_PULL_REPLY_PER_BLOCK replies for each of them.
 *
 * A timer is set to detect when nothing has been received for a while.  It is
 * updated every time a new reply is received. This timer repost requests to
 * get current blocks (using descriptors that were cached in the pull handle).
 *
 * Additionally, if the second (or more) block completes before the first one,
 * there is a good chance that one packet got lost for the former blocks.
 * In this case, we optimistically re-request the former blocks.
 * To avoid re-requesting too often, we do it only once per timeout.
 *
 * In the end, the timer is only called if:
 * + one packet is lost in all outstanding blocks
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

	dprintk_in();
	dprintk(KREF, "releasing the last reference on pull handle %p\n",
		handle);

	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_EXITED);

#if 0
	/* release the region now that we are sure that nobody else uses it */
	if (handle->xen)
		omx_xen_user_region_release(handle->xregion);
	else
#endif
	if (!handle->xen)
		omx_user_region_release(handle->region);

	kfree(handle);
	dprintk_out();
}

/*
 * Release an acquired pull handle
 */
static INLINE void
omx_pull_handle_release(struct omx_pull_handle * handle)
{
	dprintk_in();
	kref_put(&handle->refcount, __omx_pull_handle_last_release);
	dprintk_out();
}

/**************************
 * Pull Handle Index Table
 */

struct omx_pull_handle_slot {
	struct omx_pull_handle __rcu *handle;
	uint32_t id; /* index in SLOTS_BITS + a generation number in GENERATION_BITS */
	struct list_head list_elt;
};

#define OMX_PULL_HANDLE_SLOT_INDEX_BITS 10
#define OMX_PULL_HANDLE_SLOT_GENERATION_BITS (32-OMX_PULL_HANDLE_SLOT_INDEX_BITS)
#define OMX_PULL_HANDLE_SLOT_INDEX_MAX (1<<(OMX_PULL_HANDLE_SLOT_INDEX_BITS))
#define OMX_PULL_HANDLE_SLOT_INDEX_MASK ((OMX_PULL_HANDLE_SLOT_INDEX_MAX-1) << OMX_PULL_HANDLE_SLOT_GENERATION_BITS)
#define OMX_PULL_HANDLE_SLOT_GENERATION_MASK ((1<<(OMX_PULL_HANDLE_SLOT_GENERATION_BITS)) - 1)
#define OMX_PULL_HANDLE_SLOT_GENERATION_FIRST 0x23

#define OMX_PULL_HANDLE_SLOT_ID_FIRST(index) \
	((OMX_PULL_HANDLE_SLOT_GENERATION_FIRST & OMX_PULL_HANDLE_SLOT_GENERATION_MASK) \
	 + (index << OMX_PULL_HANDLE_SLOT_GENERATION_BITS))

#define OMX_PULL_HANDLE_SLOT_ID_INC(slot) do { \
	slot->id = (slot->id & OMX_PULL_HANDLE_SLOT_INDEX_MASK) \
		| ((slot->id+1) & OMX_PULL_HANDLE_SLOT_GENERATION_MASK); \
	} while (0)

#define OMX_PULL_HANDLE_SLOT_INDEX_FROM_ID(id) \
	((id) >> OMX_PULL_HANDLE_SLOT_GENERATION_BITS)
#define OMX_PULL_HANDLE_SLOT_GENERATION_FROM_ID(id) \
	((id) & OMX_PULL_HANDLE_SLOT_GENERATION_MASK)

static int
omx_pull_handle_slots_init(struct omx_endpoint *endpoint)
{
	struct omx_pull_handle_slot *slots;
	int i, ret = 0;

	dprintk_out();
	slots = kmalloc(OMX_PULL_HANDLE_SLOT_INDEX_MAX*sizeof(*slots),
			GFP_KERNEL);
	if (!slots) {
		ret = -ENOMEM;
		goto out;
	}
	endpoint->pull_handle_slots_array = slots;

	INIT_LIST_HEAD(&endpoint->pull_handle_slots_free_list);
	for(i=0; i<OMX_PULL_HANDLE_SLOT_INDEX_MAX; i++) {
		struct omx_pull_handle_slot *slot = &slots[i];
		slot->handle = NULL;
		slot->id = OMX_PULL_HANDLE_SLOT_ID_FIRST(i);
		list_add_tail(&slot->list_elt, &endpoint->pull_handle_slots_free_list);
	}

out:
	dprintk_out();
	return ret;
}

static void
omx_pull_handle_slots_exit(struct omx_endpoint *endpoint)
{
	dprintk_in();
	kfree(endpoint->pull_handle_slots_array);
	dprintk_out();
}

/*
 * Allocate the pull handle slot and associate the handle to it.
 * Returns the handle as locked.
 *
 * Called with the endpoint pull lock held
 */
static int
omx_pull_handle_alloc_slot(struct omx_endpoint *endpoint,
			   struct omx_pull_handle *handle)
{
	struct omx_pull_handle_slot *slot;
	int ret = 0;

	dprintk_in();
	if (list_empty(&endpoint->pull_handle_slots_free_list)) {
		/* FIXME: sleep */
		ret = -ENOMEM;
		goto out;
	}

	slot = list_first_entry(&endpoint->pull_handle_slots_free_list,
				struct omx_pull_handle_slot, list_elt);
	list_del(&slot->list_elt);

	/*
	 * lock the handle lock now since it may be acquired
	 * right after we assign it to this slot
	 */
	spin_lock(&handle->lock);

	handle->slot_id = slot->id;
	rcu_assign_pointer(slot->handle, handle);

	dprintk(PULL, "allocating slot index %d generation %d for pull handle %p\n",
		(unsigned) OMX_PULL_HANDLE_SLOT_INDEX_FROM_ID(slot->id),
		(unsigned) OMX_PULL_HANDLE_SLOT_GENERATION_FROM_ID(slot->id),
		handle);

	/* tell the sparse checker that the handle returned as locked */
	__release(&handle->lock);

out:
	dprintk_out();
	return ret;
}

/*
 * Free a pull handle slot.
 *
 * Called with the endpoint pull lock held
 */
static void
omx_pull_handle_free_slot(struct omx_endpoint *endpoint,
			  struct omx_pull_handle *handle)
{
	struct omx_pull_handle_slot *array = endpoint->pull_handle_slots_array;
	uint32_t index = OMX_PULL_HANDLE_SLOT_INDEX_FROM_ID(handle->slot_id);
	struct omx_pull_handle_slot *slot = &array[index];

	dprintk_in();
	dprintk(PULL, "freeing slot index %d generation %d from pull handle %p\n",
		(unsigned) OMX_PULL_HANDLE_SLOT_INDEX_FROM_ID(slot->id),
		(unsigned) OMX_PULL_HANDLE_SLOT_GENERATION_FROM_ID(slot->id),
		handle);

	RCU_INIT_POINTER(slot->handle, NULL);
	list_add_tail(&slot->list_elt, &endpoint->pull_handle_slots_free_list);
	/* FIXME: wakeup one sleeper */

	OMX_PULL_HANDLE_SLOT_ID_INC(slot);
	dprintk_out();
}

/*
 * Find a pull handle slot using an id coming from the wire.
 *
 * Called withOUT the endpoint pull lock held, uses RCU
 */
static struct omx_pull_handle *
omx_pull_handle_acquire_from_slot(struct omx_endpoint *endpoint,
				  uint32_t slot_id)
{
	struct omx_pull_handle_slot *array = endpoint->pull_handle_slots_array;
	struct omx_pull_handle_slot *slot;
	struct omx_pull_handle *handle, *ret;
	uint32_t index = OMX_PULL_HANDLE_SLOT_INDEX_FROM_ID(slot_id);

	dprintk_in();
	if (unlikely(index >= OMX_PULL_HANDLE_SLOT_INDEX_MAX)) {
		ret = NULL;
		goto out;
	}

	slot = &array[index];

	rcu_read_lock();

	dprintk(PULL, "looking for slot index %d generation %d\n",
		(unsigned) index, (unsigned) OMX_PULL_HANDLE_SLOT_GENERATION_FROM_ID(slot_id));

	handle = rcu_dereference(slot->handle);
	if (!handle) {
		dprintk(PULL, "slot index %d not used by any pull handle\n", index);
		goto out_with_rcu;
	}

	if (slot_id != slot->id) {
		dprintk(PULL, "slot index %d has generation %d instead of %d\n",
			(unsigned) index,
			(unsigned) OMX_PULL_HANDLE_SLOT_GENERATION_FROM_ID(slot->id),
			(unsigned) OMX_PULL_HANDLE_SLOT_GENERATION_FROM_ID(slot_id));
		handle = NULL;
		goto out_with_rcu;
	}

	omx_pull_handle_acquire(handle);

 out_with_rcu:
	rcu_read_unlock();
	ret = handle;
out:
	dprintk_out();
	return ret;
}

/***************************************
 * Per-endpoint pull handles management
 */

int
omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint)
{
	dprintk_in();
	INIT_LIST_HEAD(&endpoint->pull_handles_list);
	omx_pull_handle_slots_init(endpoint);
	spin_lock_init(&endpoint->pull_handles_lock);
	dprintk_out();
	return 0;
}

/*
 * Called when the endpoint starts to be closed.
 */
void
omx_endpoint_pull_handles_exit(struct omx_endpoint * endpoint)
{
	dprintk_in();
	might_sleep();

	/*
	 * ask all pull handles of the endpoint to stop their timer.
	 * but we can't take endpoint->pull_handles_lock before handle->lock since that would deadlock
	 * so we use a tricky loop to take locks in order
	 */

	spin_lock_bh(&endpoint->pull_handles_lock);
	while (!list_empty(&endpoint->pull_handles_list)) {
		struct omx_pull_handle * handle;
		int ret;

		/* get the first handle of the list, acquire it and release the list lock */
		handle = list_first_entry(&endpoint->pull_handles_list, struct omx_pull_handle, list_elt);
		omx_pull_handle_acquire(handle);
		spin_unlock_bh(&endpoint->pull_handles_lock);

		/* take the handle lock and check the status in case it changed while the lock was released */
		spin_lock_bh(&handle->lock);
		if (handle->status == OMX_PULL_HANDLE_STATUS_OK) {
			/* the handle didn't change, remove from the slot array so that no incoming packet can find it anymore */
			dprintk(PULL, "(endpoint close) removing pull handle %p from slot array\n", handle);
			spin_lock(&endpoint->pull_handles_lock);
			omx_pull_handle_free_slot(endpoint, handle);
			spin_unlock(&endpoint->pull_handles_lock);

			/* release the lock and wait for the timer to exit */
			handle->status = OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT;
		}
		spin_unlock_bh(&handle->lock);

		dprintk(PULL, "(endpoint close) stopping handle %p timer with del_sync_timer\n", handle);
		ret = del_timer_sync(&handle->retransmit_timer);
		if (ret) {
			/* we deactivated the timer, cleanup ourself */
			spin_lock_bh(&handle->lock);

			/* mark the timer as exited */
			dprintk(PULL, "(endpoint close) del_timer_sync stopped pull handle %p timer\n", handle);
			BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT);
			handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;

			/* remove from the list */
			spin_lock(&endpoint->pull_handles_lock);
			list_del(&handle->list_elt);
			spin_unlock(&endpoint->pull_handles_lock);

			/* release the timer reference on the pull handle and endpoint */
			spin_unlock_bh(&handle->lock);
			omx_pull_handle_release(handle);
			omx_endpoint_release(endpoint);

		} else {
			/* the timer expired meanwhile, the handle is already removed from the list */
			BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_EXITED);
			dprintk(PULL, "(endpoint close) pull handle %p timer already exited\n", handle);
		}

		omx_pull_handle_release(handle);

		/* take the list lock back before processing another handle */
		spin_lock_bh(&endpoint->pull_handles_lock);
	}
	spin_unlock_bh(&endpoint->pull_handles_lock);

	omx_pull_handle_slots_exit(endpoint);
	dprintk_out();
}

/************************
 * Pull handles creation
 */

static INLINE int
omx_pull_handle_pkt_hdr_fill(const struct omx_endpoint * endpoint,
			     struct omx_pull_handle * handle,
			     const struct omx_cmd_pull * cmd)
{
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct omx_hdr * mh = &handle->pkt_hdr;
	struct omx_pkt_head * ph = &mh->head;
	struct ethhdr * eh = &ph->eth;
	struct omx_pkt_pull_request * pull_n = &mh->body.pull;
	int ret;

	dprintk_in();
	/* pre-fill the packet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd->peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in pull request header\n");
		goto out;
	}

	/* fill omx header */
	OMX_HTON_8(pull_n->ptype, OMX_PKT_TYPE_PULL);
	OMX_HTON_8(pull_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(pull_n->dst_endpoint, cmd->dest_endpoint);
	OMX_HTON_32(pull_n->session, cmd->session_id);
	OMX_HTON_32(pull_n->total_length, handle->total_length);
#ifdef OMX_MX_WIRE_COMPAT
	OMX_HTON_8(pull_n->pulled_rdma_id, cmd->pulled_rdma_id);
	OMX_HTON_16(pull_n->pulled_rdma_offset, handle->pulled_rdma_offset);
#else
	OMX_HTON_32(pull_n->pulled_rdma_id, cmd->pulled_rdma_id);
	OMX_HTON_32(pull_n->pulled_rdma_offset, handle->pulled_rdma_offset);
#endif
	OMX_HTON_8(pull_n->pulled_rdma_seqnum, cmd->pulled_rdma_seqnum);
	OMX_HTON_32(pull_n->src_pull_handle, handle->slot_id);
	OMX_HTON_32(pull_n->src_magic, endpoint->endpoint_index ^ OMX_ENDPOINT_PULL_MAGIC_XOR);

	/* block_length, frame_index, and first_frame_offset filled at actual send */

	ret = 0;
	goto out;

 out:
	dprintk_out();
	return ret;
}

/*
 * Create a pull handle and return it as acquired and locked.
 */
static INLINE struct omx_pull_handle *
omx_pull_handle_create(struct omx_endpoint * endpoint,
		       const struct omx_user_region * region,
		       const struct omx_xen_user_region * xregion,
		       const struct omx_cmd_pull * cmd)
{
	struct omx_pull_handle * handle;
	int i;
	int err;

	dprintk_in();
	/* alloc the pull handle */
	handle = kmalloc(sizeof(struct omx_pull_handle), GFP_KERNEL);
	if (unlikely(!handle)) {
		printk(KERN_INFO "Open-MX: Failed to allocate a pull handle\n");
		err = -ENOMEM;
		goto out;
	}
	/* initialize the lock, we will acquire it soon */
	spin_lock_init(&handle->lock);

	spin_lock_bh(&endpoint->pull_handles_lock);

	err = omx_pull_handle_alloc_slot(endpoint, handle);
	if (unlikely(err < 0)) {
		dprintk(PULL, "Open-MX: Failed to find a slot for pull handle\n");
		spin_unlock_bh(&endpoint->pull_handles_lock);
		err = -ENOMEM;
		goto out_with_handle;
	}

	/* tell the sparse checker that the handle returned as locked */
	__acquire(&handle->lock);

	/* we are good now, finish filling the handle */
	kref_init(&handle->refcount); /* the timer's reference */
	handle->endpoint = endpoint;
	if (xregion) {
		dprintk_deb("Xen handle\n");
		handle->xen = 1;
		handle->region = NULL;
		handle->xregion = (struct omx_xen_user_region *) xregion;
	}
	else
	{
		dprintk_deb("Normal handle\n");
		handle->xen = 0;
		handle->region = (struct omx_user_region *) region;
		handle->xregion = NULL;
	}
	handle->total_length = cmd->length;
	handle->pulled_rdma_offset = cmd->pulled_rdma_offset;

	/* initialize variable stuff */
	handle->status = OMX_PULL_HANDLE_STATUS_OK;
	handle->remaining_length = cmd->length;
	handle->frame_index = 0;
	handle->next_frame_index = 0;
	handle->nr_requested_frames = 0;
	handle->nr_missing_frames = 0;
	handle->nr_valid_block_descs = 0;
	for(i=0; i<OMX_PULL_BLOCK_DESCS_NR-1; i++)
		handle->block_desc[i].frames_missing_bitmap = 0; /* make sure the invalid block descs are easy to check */
	handle->already_rerequested_blocks = 0;
	handle->last_retransmit_jiffies = get_jiffies_64() + cmd->resend_timeout_jiffies;

	handle->host_copy_nr_frames = 0;

#ifdef OMX_HAVE_DMA_ENGINE
	handle->dma_copy_chan = NULL;
	handle->dma_copy_last_cookie = -1;
	skb_queue_head_init(&handle->dma_copy_skb_queue);
	OMX_INIT_WORK(&handle->dma_copy_deferred_wait_work,
		      omx_pull_handle_deferred_dma_completions_wait_workfunc,
		      handle);
#endif

	/* initialize the completion event */
	handle->done_event.id = 0;
	handle->done_event.type = OMX_EVT_PULL_DONE;
	handle->done_event.puller_rdma_id = cmd->puller_rdma_id;
	handle->done_event.lib_cookie = cmd->lib_cookie;

	/* initialize cached header */
	err = omx_pull_handle_pkt_hdr_fill(endpoint, handle, cmd);
	if (err < 0)
		goto out_with_slot;

	/* init timer */
	setup_timer(&handle->retransmit_timer, omx_pull_handle_timeout_handler,
		    (unsigned long) handle);
	omx_endpoint_reacquire(endpoint); /* keep a reference for the timer */

	/* queue in the endpoint list */
	list_add_tail(&handle->list_elt, &endpoint->pull_handles_list);

	spin_unlock_bh(&endpoint->pull_handles_lock);

	dprintk(PULL, "created and acquired pull handle %p\n", handle);

	/* tell the sparse checker that we keep the lock and pass it back to the caller */
	__release(&handle->lock);

	dprintk_out();
	return handle;

 out_with_slot:
	omx_pull_handle_free_slot(endpoint, handle);
	spin_unlock_bh(&endpoint->pull_handles_lock);
	spin_unlock(&handle->lock);
 out_with_handle:
	kfree(handle);
 out:
	dprintk_out();
	return ERR_PTR(err);
}

/*************************
 * Pull handle completion
 */

/*
 * Takes an acquired and locked pull handle, unhash it and set its status.
 * Called by the BH after receiving a pull reply or a nack,
 * or by the retransmit timer when expired.
 *
 * If the timeout expired, status is OMX_EVT_PULL_DONE_TIMEOUT
 * and the timer will exit right after returning from here.
 * In the other cases, the timer needs to catch TIMER_MUST_EXIT.
 */
static INLINE void
omx_pull_handle_mark_completed(struct omx_pull_handle * handle, uint8_t status)
{
	struct omx_endpoint * endpoint = handle->endpoint;

	dprintk_in();
	/* tell the sparse checker that the caller took the lock */
	__acquire(&handle->lock);

	BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);
	handle->status = OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT;

	/* remove from the slot array so that no incoming packet can find it anymore */
	spin_lock_bh(&endpoint->pull_handles_lock);
	omx_pull_handle_free_slot(endpoint, handle);
	spin_unlock_bh(&endpoint->pull_handles_lock);

	/* finish filling the event for user-space */
	/* enforce that nack type and pull status have same values */
	BUILD_BUG_ON(OMX_EVT_PULL_DONE_BAD_ENDPT != OMX_NACK_TYPE_BAD_ENDPT);
	BUILD_BUG_ON(OMX_EVT_PULL_DONE_ENDPT_CLOSED != OMX_NACK_TYPE_ENDPT_CLOSED);
	BUILD_BUG_ON(OMX_EVT_PULL_DONE_BAD_SESSION != OMX_NACK_TYPE_BAD_SESSION);
	BUILD_BUG_ON(OMX_EVT_PULL_DONE_BAD_RDMAWIN != OMX_NACK_TYPE_BAD_RDMAWIN);
	handle->done_event.status = status;

	/* tell the sparse checker that the caller took the lock */
	__release(&handle->lock);
	dprintk_out();
}

/*
 * Notify handle completion to user-space now that all pending stuff are done.
 *
 * The handle lock must not be held, but the handle must still be acquired.
 */
static INLINE void
omx_pull_handle_notify(struct omx_pull_handle * handle)
{
	struct omx_endpoint * endpoint = handle->endpoint;

	dprintk_in();
	if (endpoint->xen) {
		omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
		struct omx_xenif_response *ring_resp;
		dprintk(PULL, "XEN ENDPOINT! PULL DONE!@%#lx\n", (unsigned long) omx_xenif);

                ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		ring_resp->func = OMX_CMD_XEN_RECV_PULL_DONE;
		ring_resp->data.recv_pull_done.board_index = endpoint->board_index;
		ring_resp->data.recv_pull_done.eid = endpoint->endpoint_index;
		ring_resp->data.recv_pull_done.rid = handle->xregion->id;
		//printk_inf("region id = %#x\n", handle->xregion->id);
		memcpy(&ring_resp->data.recv_pull_done.pull_done, &handle->done_event, sizeof(handle->done_event));

		dump_xen_recv_pull_done(&ring_resp->data.recv_pull_done);
		//omx_xen_user_region_release(handle->xregion);

		//omx_xen_user_region_destroy_segments(handle->xregion, endpoint);
		//rcu_assign_pointer(endpoint->xen_regions[handle->xregion->id], NULL);
		//kfree(handle->xregion);
		omx_poke_domU(omx_xenif, OMX_CMD_XEN_RECV_PULL_DONE, ring_resp);
	} else

	{
		omx_notify_exp_event(endpoint,
				     &handle->done_event, sizeof(handle->done_event));

		/* release the handle */
		omx_pull_handle_release(handle);
	}
	omx_endpoint_release(endpoint);

	/*
	 * do not release the region here, let the last pull user release it.
	 * if we are completing the pull with an error, there could be other users in memcpy
	 */
	dprintk_out();
}

/*
 * Notify handle completion to user-space, using a deferred work to wait
 * for all pending stuff to be done first.
 *
 * The handle lock must not be held, but the handle must still be acquired.
 */
static INLINE void
omx_pull_handle_bh_notify(struct omx_pull_handle * handle)
{
	int ret;

	/* see if the offloaded copies are done */
	ret = omx_pull_handle_deferred_wait_dma_completions(handle);
	if (!ret)
		omx_pull_handle_notify(handle);
}

/**************************************
 * Pull handle frame bitmap management
 */

static INLINE void
omx_pull_handle_append_needed_frames(struct omx_pull_handle * handle,
				     uint32_t block_length,
				     uint32_t first_frame_offset)
{
	struct omx_pull_block_desc *desc;
	omx_block_frame_bitmask_t new_mask;
	int new_frames;

	dprintk_in();
	new_frames = (first_frame_offset + block_length
		      + OMX_PULL_REPLY_LENGTH_MAX-1) / OMX_PULL_REPLY_LENGTH_MAX;
	new_mask = ((omx_block_frame_bitmask_t) -1) >> (OMX_PULL_REPLY_PER_BLOCK-new_frames);

	desc = &handle->block_desc[handle->nr_valid_block_descs];
	desc->frame_index = handle->next_frame_index;
	desc->block_length = block_length;
	desc->first_frame_offset = first_frame_offset;
	desc->frames_missing_bitmap = new_mask;

	handle->nr_requested_frames += new_frames;
	handle->nr_missing_frames += new_frames;
	handle->next_frame_index += new_frames;
	handle->remaining_length -= block_length;
	handle->nr_valid_block_descs++;

	dprintk(PULL, "appending block #%d with %d new frames to pull handle %p, now requested %ld-%ld\n",
		handle->nr_valid_block_descs-1, new_frames, handle,
		(unsigned long) handle->frame_index, (unsigned long) handle->next_frame_index-1);
	dprintk_out();
}

static INLINE void
omx_pull_handle_first_block_done(struct omx_pull_handle * handle)
{
	uint32_t first_block_frames = min(handle->nr_requested_frames, (uint32_t) OMX_PULL_REPLY_PER_BLOCK);

	dprintk_in();
	handle->frame_index += first_block_frames;
	handle->nr_requested_frames -= first_block_frames;
	handle->nr_valid_block_descs--;
	if (handle->already_rerequested_blocks)
		handle->already_rerequested_blocks--;
	memmove(&handle->block_desc[0], &handle->block_desc[1],
		sizeof(struct omx_pull_block_desc) * handle->nr_valid_block_descs);
	handle->block_desc[OMX_PULL_BLOCK_DESCS_NR-1].frames_missing_bitmap = 0; /* make sure the invalid block descs are easy to check */

	dprintk(PULL, "first block of pull handle %p done, removing %d requested frames, now requested %ld-%ld\n",
		handle, first_block_frames,
		(unsigned long) handle->frame_index, (unsigned long) handle->next_frame_index-1);
	dprintk_out();
}

/************************
 * Sending pull requests
 */

/* Called with the handle acquired and locked */
static INLINE struct sk_buff *
omx_fill_pull_block_request(const struct omx_pull_handle * handle, int desc_nr)
{
	const struct omx_pull_block_desc * desc = &handle->block_desc[desc_nr];
	struct omx_iface * iface = handle->endpoint->iface;
	uint32_t frame_index = desc->frame_index;
	uint32_t block_length = desc->block_length;
	uint32_t first_frame_offset = desc->first_frame_offset;
	struct sk_buff * skb;
	struct omx_hdr * mh;
	struct omx_pkt_pull_request * pull_n;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_request);

	dprintk_in();
	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create pull skb\n");
		dprintk_out();
		return ERR_PTR(-ENOMEM);
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	pull_n = &mh->body.pull;

	/* copy common pkt hdrs from the handle */
	memcpy(mh, &handle->pkt_hdr, sizeof(handle->pkt_hdr));

#ifdef OMX_MX_WIRE_COMPAT
	OMX_HTON_16(pull_n->block_length, block_length);
	OMX_HTON_16(pull_n->first_frame_offset, first_frame_offset);
#else
	OMX_HTON_32(pull_n->block_length, block_length);
	OMX_HTON_32(pull_n->first_frame_offset, first_frame_offset);
#endif
	OMX_HTON_32(pull_n->frame_index, frame_index);

	omx_send_dprintk(&mh->head.eth, "PULL handle %lx magic %lx length %ld out of %ld, frame index %ld first_frame_offset %ld",
			 (unsigned long) OMX_NTOH_32(pull_n->src_pull_handle),
			 (unsigned long) OMX_NTOH_32(pull_n->src_magic),
			 (unsigned long) block_length,
			 (unsigned long) OMX_NTOH_32(pull_n->total_length),
			 (unsigned long) frame_index,
			 (unsigned long) first_frame_offset);

	dprintk_out();
	return skb;
}

int
omx_ioctl_pull(struct omx_endpoint * endpoint,
	       void __user * uparam)
{
	struct omx_cmd_pull cmd;
	struct omx_pull_handle * handle;
	struct omx_user_region * region = NULL;
	struct omx_xen_user_region * xregion = NULL;
	struct omx_iface * iface = endpoint->iface;
	struct sk_buff * skb, * skbs[] = { [0 ... OMX_PULL_BLOCK_DESCS_NR-1] = NULL };
	uint32_t block_length;
	uint32_t pulled_rdma_offset_in_frame;
	int i;
	int err = 0;

	dprintk_in();
	TIMER_START(&t_pull);
	err = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send pull cmd hdr\n");
		err = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.shared)) {
		err = omx_shared_pull(endpoint, &cmd);
		goto out;
	}

	if (endpoint->xen) {
		dprintk_deb("Xen endpoint!\n");
		/* acquire the region */
		xregion = omx_xen_user_region_acquire(endpoint, cmd.puller_rdma_id);
		if (unlikely(!xregion)) {
			printk_err("acquire region\n");
			err = -EINVAL;
			goto out;
		}
	}
	else {
		//BUG(); /* never reach this point ATM */
		/* acquire the region */
		region = omx_user_region_acquire(endpoint, cmd.puller_rdma_id);
		if (unlikely(!region)) {
			err = -EINVAL;
			goto out;
		}

		region->dirty = 1;

		if (!omx_pin_synchronous) {
			/* make sure the region is pinned */
			struct omx_user_region_pin_state pinstate;

			omx_user_region_demand_pin_init(&pinstate, region);
			pinstate.next_chunk_pages = omx_pin_chunk_pages_max;
			err = omx_user_region_demand_pin_finish(&pinstate);
			/* no progressive/demand-pinning for native networking */
			if (err < 0) {
				dprintk(REG, "failed to pin user region\n");
				goto out_with_region;
			}
		}

	}
	/* create, acquire and lock the handle */
	handle = omx_pull_handle_create(endpoint, region, xregion, &cmd);
	if (IS_ERR(handle)) {
		printk_err("error in handle_create\n");
		err = PTR_ERR(handle);
		goto out_with_region;
	}


	/* tell the sparse checker that the lock has been taken by omx_pull_handle_create() */
	__acquire(&handle->lock);

	/* send a first pull block request,
	 * ignoring the frames that are before the pull request beginning
	 * since we want to manipulate an actual msg offset
	 * and we want some full blocks
	 */
	pulled_rdma_offset_in_frame = handle->pulled_rdma_offset % OMX_PULL_REPLY_LENGTH_MAX;
	block_length = OMX_PULL_BLOCK_LENGTH_MAX - pulled_rdma_offset_in_frame;
	if (block_length > handle->remaining_length)
		block_length = handle->remaining_length;

	omx_pull_handle_append_needed_frames(handle, block_length, pulled_rdma_offset_in_frame);

	/* prepare as many new blocks as needed */
	while (handle->nr_valid_block_descs < OMX_PULL_BLOCK_DESCS_NR
	       && handle->remaining_length) {
		/* prepare the next block */
		block_length = OMX_PULL_BLOCK_LENGTH_MAX;
		if (block_length > handle->remaining_length)
			block_length = handle->remaining_length;
		omx_pull_handle_append_needed_frames(handle, block_length, 0);
	}

	/* try to actually request the needed new blocks */
	for(i=0; i<handle->nr_valid_block_descs; i++) {
		if (i > 0)
			dprintk(PULL, "queueing another pull block request\n");
		else
			dprintk(PULL, "queueing pull block request\n");

		skb = omx_fill_pull_block_request(handle, i);
		if (unlikely(IS_ERR(skb))) {
			BUG_ON(PTR_ERR(skb) != -ENOMEM);
			/* let the timeout expire and resend */
			goto skbs_ready;
		} else {
			skbs[i] = skb;
		}
	}

 skbs_ready:
	/* schedule the timeout handler now that we are ready to send the requests */
	/* timer not pending yet, use the regular mod_timer() */
	mod_timer(&handle->retransmit_timer,
		  get_jiffies_64() + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	/*
	 * do not keep the lock while sending
	 * since the loopback device may cause reentrancy
	 */
	spin_unlock(&handle->lock);

	for(i=0; i<OMX_PULL_BLOCK_DESCS_NR; i++)
		if (likely(skbs[i]))
			omx_queue_xmit(iface, skbs[i], PULL_REQ);

	err = 0;
	goto out;

 out_with_region:
	if (endpoint->xen)
		omx_xen_user_region_release(xregion);
	else
		omx_user_region_release(region);
 out:
	TIMER_STOP(&t_pull);
	dprintk_out();
	return err;
}

/*************************
 * Handle timeout handler
 */

static INLINE void
omx_progress_pull_on_handle_timeout_handle_locked(struct omx_iface * iface,
						  struct omx_pull_handle * handle)
{
	struct sk_buff *skb, *skbs[] = { [0 ... OMX_PULL_BLOCK_DESCS_NR-1] = NULL };
	int i;

	dprintk_in();
	/* tell the sparse checker that the lock has been taken by the caller */
	__acquire(&handle->lock);

	/* request the first block again */
	omx_counter_inc(iface, PULL_TIMEOUT_HANDLER_FIRST_BLOCK);

	skb = omx_fill_pull_block_request(handle, 0);
	if (unlikely(IS_ERR(skb))) {
		BUG_ON(PTR_ERR(skb) != -ENOMEM);
		goto skbs_ready; /* don't try to submit more */
	} else {
		skbs[0] = skb;
		handle->already_rerequested_blocks = 0;
	}

	/*
	 * If the other blocks aren't done either, request it again
	 * (otherwise the N-block pipeline would be broken for ever)
	 * This shouldn't happen often since it means a packet has been lost
	 * in each block.
	 */
	for(i=1; i<OMX_PULL_BLOCK_DESCS_NR; i++) {
		if (handle->block_desc[i].frames_missing_bitmap) {
			omx_counter_inc(iface, PULL_TIMEOUT_HANDLER_NONFIRST_BLOCK);

			skb = omx_fill_pull_block_request(handle, i);
			if (unlikely(IS_ERR(skb))) {
				BUG_ON(PTR_ERR(skb) != -ENOMEM);
				goto skbs_ready; /* don't try to submit more */
			} else {
				skbs[i] = skb;
			}
		}
	}

 skbs_ready:
	/* cleanup a bit of dma-offloaded copies */
	omx_pull_handle_poll_dma_completions(handle);

	/* reschedule another timeout handler */
	/* timer already expired, use the regular mod_timer() */
	mod_timer(&handle->retransmit_timer,
		  get_jiffies_64() + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	/*
	 * do not keep the lock while sending
	 * since the loopback device may cause reentrancy
	 */
	spin_unlock(&handle->lock);

	for(i=0; i<OMX_PULL_BLOCK_DESCS_NR; i++)
		if (likely(skbs[i]))
			omx_queue_xmit(iface, skbs[i], PULL_REQ);
	dprintk_out();
}

/*
 * Retransmission callback, owns a reference on the handle and endpoint.
 * Running as long as status is OMX_PULL_HANDLE_STATUS_OK.
 */
static void
omx_pull_handle_timeout_handler(unsigned long data)
{
	struct omx_pull_handle * handle = (void *) data;
	struct omx_endpoint * endpoint = handle->endpoint;
	struct omx_iface * iface = endpoint->iface;

	dprintk_in();
	dprintk(PULL, "pull handle %p timer reached, might need to request again\n", handle);

	spin_lock(&handle->lock);

	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT);

		dprintk(PULL, "pull handle %p timer exiting on close request\n", handle);
		spin_lock_bh(&endpoint->pull_handles_lock);
		list_del(&handle->list_elt);
		spin_unlock_bh(&endpoint->pull_handles_lock);

		handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;
		spin_unlock(&handle->lock);

		omx_pull_handle_release(handle);
		omx_endpoint_release(endpoint);

		goto out; /* timer will never be called again (status is TIMER_EXITED) */
	}

	if (time_after64(get_jiffies_64(), handle->last_retransmit_jiffies)) {
		BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_OK);

		dprintk(PULL, "pull handle %p last retransmit time reached, reporting an error\n", handle);
		omx_counter_inc(iface, PULL_TIMEOUT_ABORT);

		omx_pull_handle_mark_completed(handle, OMX_EVT_PULL_DONE_TIMEOUT);

		dprintk(PULL, "pull handle %p timer done, removing from endpoint list\n", handle);
		spin_lock_bh(&endpoint->pull_handles_lock);
		list_del(&handle->list_elt);
		spin_unlock_bh(&endpoint->pull_handles_lock);

		BUG_ON(handle->status != OMX_PULL_HANDLE_STATUS_TIMER_MUST_EXIT);
		handle->status = OMX_PULL_HANDLE_STATUS_TIMER_EXITED;

		/* nobody is going to use this handle, no need to lock anymore */
		spin_unlock(&handle->lock);

		/* let notify release the handle and endpoint */
		omx_pull_handle_bh_notify(handle);

		goto out; /* timer will never be called again (status is TIMER_EXITED) */
	}

	BUG_ON(!handle->block_desc[0].frames_missing_bitmap);

	/* request more replies if necessary */
	omx_progress_pull_on_handle_timeout_handle_locked(iface, handle);
	/* tell sparse checker that the lock has been released by omx_progress_pull_on_handle_timeout_handle_locked() */
	__release(&handle->lock);
out:
	dprintk_out();
}

/*******************************************
 * Recv pull requests and send pull replies
 */

/* pull reply skb destructor to release the user region */
static void
omx_send_pull_reply_skb_destructor(struct sk_buff *skb)
{
	struct omx_user_region * region = omx_get_skb_destructor_data(skb);
	dprintk_in();
	omx_user_region_release(region);
	dprintk_out();
}


/* xen pull reply skb destructor to release the user region */
static void
omx_xen_send_pull_reply_skb_destructor(struct sk_buff *skb)
{
	struct omx_xen_user_region * region = omx_get_skb_destructor_data(skb);
	dprintk_in();
	omx_xen_user_region_release(region);
	dprintk_out();
}

int
omx_recv_pull_request(struct omx_iface * iface,
		      struct omx_hdr * pull_mh,
		      struct sk_buff * orig_skb)
{
	struct net_device * ifp = iface->eth_ifp;
	struct omx_endpoint * endpoint;
	struct omx_pkt_head *pull_ph = &pull_mh->head;
	struct ethhdr *pull_eh = &pull_ph->eth;
	struct omx_pkt_pull_request *pull_request_n = &pull_mh->body.pull;
	uint8_t dst_endpoint = OMX_NTOH_8(pull_request_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(pull_request_n->src_endpoint);
	uint32_t session_id = OMX_NTOH_32(pull_request_n->session);
#ifdef OMX_MX_WIRE_COMPAT
	uint32_t block_length = OMX_NTOH_16(pull_request_n->block_length);
	uint32_t first_frame_offset = OMX_NTOH_16(pull_request_n->first_frame_offset);
	uint32_t pulled_rdma_id = OMX_NTOH_8(pull_request_n->pulled_rdma_id);
	uint32_t pulled_rdma_offset = OMX_NTOH_16(pull_request_n->pulled_rdma_offset);
#else
	uint32_t block_length = OMX_NTOH_32(pull_request_n->block_length);
	uint32_t first_frame_offset = OMX_NTOH_32(pull_request_n->first_frame_offset);
	uint32_t pulled_rdma_id = OMX_NTOH_32(pull_request_n->pulled_rdma_id);
	uint32_t pulled_rdma_offset = OMX_NTOH_32(pull_request_n->pulled_rdma_offset);
#endif
	uint32_t src_pull_handle = OMX_NTOH_32(pull_request_n->src_pull_handle);
	uint32_t src_magic = OMX_NTOH_32(pull_request_n->src_magic);
	uint32_t frame_index = OMX_NTOH_32(pull_request_n->frame_index);
	uint16_t peer_index = OMX_NTOH_16(pull_mh->head.dst_src_peer_index);
	struct omx_user_region_offset_cache region_cache;
	struct omx_pkt_pull_reply *pull_reply_n;
	struct omx_hdr *reply_mh;
	struct omx_pkt_head *reply_ph;
	struct ethhdr *reply_eh;
	size_t reply_hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	struct omx_user_region *region = NULL;
	struct omx_xen_user_region *xregion = NULL;
	uint32_t current_frame_seqnum, current_msg_offset, block_remaining_length;
	int replies, i;
	int err = 0;

	dprintk_in();
	TIMER_START(&t_pull_request);
#if 0
	dprintk_deb("%s: delaying on purpose to understand what is going on!\n", __func__);
	udelay(10000);
#endif
	BUILD_BUG_ON(OMX_PULL_REPLY_PACKET_SIZE_OF_PAYLOAD(OMX_PULL_REPLY_LENGTH_MAX) > OMX_MTU);

	omx_counter_inc(iface, RECV_PULL_REQ);

        /* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(pull_eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(pull_eh, "PULL packet with wrong peer index %d",
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
		dprintk_deb("session_id = %#x, endpoint->session_id = %#lx\n", session_id, endpoint->session_id);
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
			 (unsigned long) OMX_NTOH_32(pull_request_n->total_length),
			 (unsigned long) frame_index,
			 (unsigned long) first_frame_offset);

	dprintk_deb("PULL handle %lx magic %lx length %ld out of %ld, index %ld first_frame_offset %ld",
			 (unsigned long) src_pull_handle,
			 (unsigned long) src_magic,
			 (unsigned long) block_length,
			 (unsigned long) OMX_NTOH_32(pull_request_n->total_length),
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
	dprintk_deb("replies = %d!\n", replies);

	if (endpoint->xen) {
		dprintk_deb("Xen endpoint!!!!! none of this matters!\n");

		xregion = omx_xen_user_region_acquire(endpoint, pulled_rdma_id);
		if (unlikely(!xregion)) {
			omx_counter_inc(iface, DROP_PULL_BAD_REGION);
			omx_drop_dprintk(pull_eh, "XEN PULL packet with bad xregion");
			omx_send_nack_mcp(iface, peer_index,
					  OMX_NACK_TYPE_BAD_RDMAWIN,
					  src_endpoint, src_pull_handle, src_magic);
			err = -EINVAL;
			goto xen_out;
		}
		/* initialize pull reply fields */
		current_frame_seqnum = frame_index;
		current_msg_offset = frame_index * OMX_PULL_REPLY_LENGTH_MAX
			- (pulled_rdma_offset % OMX_PULL_REPLY_LENGTH_MAX) /* hide the first frames that ignored in this pull since we want an actual msg offset */
			+ first_frame_offset;
		block_remaining_length = block_length;

		/* initialize the region offset cache and check length/offset */
		err = omx_xen_user_region_offset_cache_init(xregion, &region_cache,
							current_msg_offset + pulled_rdma_offset, block_length);
		if (err < 0) {
			omx_counter_inc(iface, DROP_PULL_BAD_OFFSET_LENGTH);
			omx_drop_dprintk(pull_eh, "XEN PULL packet due to wrong offset/length");
			/* no nack but the wire proto should be fixed for this */
			err = -EINVAL;
			omx_xen_user_region_release(xregion);
			goto xen_out;
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
				     || !omx_skb_frags)) {
				dprintk(PULL, "will not append pages to pull reply, instead, we'll use a linear skb\n");
				goto linear_xen;
			}

			/* allocate an skb */
			skb = omx_new_skb(/* only allocate space for the header now, we'll attach pages later */
					  reply_hdr_len);
			if (unlikely(skb == NULL)) {
				omx_counter_inc(iface, SEND_NOMEM_SKB);
				omx_drop_dprintk(pull_eh, "PULL packet due to failure to create pull reply skb");
				err = -ENOMEM;
				omx_xen_user_region_release(xregion);
				omx_endpoint_release(endpoint);
				goto out;
			}

			dprintk_deb("frame_length = %#x\n", frame_length);
			/* append segment pages */
			err = region_cache.append_pages_to_skb(&region_cache, skb, frame_length);
			if (likely(!err)) {
				/* successfully appended frags */

				/* reacquire the region and keep the reference for the destructor */
				kref_get(&xregion->refcount);
				omx_set_skb_destructor(skb, omx_xen_send_pull_reply_skb_destructor, xregion);

				/* locate headers */
				reply_mh = omx_skb_mac_header(skb);
				reply_ph = &reply_mh->head;
				reply_eh = &reply_ph->eth;

			} else {
				void *data;
				dprintk(PULL, "err in appending pages to pull reply, reverting to a linear skb\n");

				dev_kfree_skb(skb);

linear_xen:
				/* failed to append, revert back to copy into a linear skb */
				omx_counter_inc(iface, PULL_REPLY_SEND_LINEAR);

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
				reply_mh = omx_skb_mac_header(skb);
				reply_ph = &reply_mh->head;
				reply_eh = &reply_ph->eth;
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
			OMX_HTON_32(pull_reply_n->msg_offset, current_msg_offset);
			OMX_HTON_8(pull_reply_n->frame_seqnum, current_frame_seqnum);
			OMX_HTON_16(pull_reply_n->frame_length, frame_length);
			OMX_HTON_8(pull_reply_n->ptype, OMX_PKT_TYPE_PULL_REPLY);
			OMX_HTON_32(pull_reply_n->dst_pull_handle, src_pull_handle);
			OMX_HTON_32(pull_reply_n->dst_magic, src_magic);

			omx_send_dprintk(reply_eh, "PULL REPLY #%d handle %lx magic %lx frame seqnum %ld length %ld offset %ld", i,
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
		omx_xen_user_region_release(xregion);

		goto xen_out;
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
		- (pulled_rdma_offset % OMX_PULL_REPLY_LENGTH_MAX) /* hide the first frames that ignored in this pull since we want an actual msg offset */
		+ first_frame_offset;
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
			reply_mh = omx_skb_mac_header(skb);
			reply_ph = &reply_mh->head;
			reply_eh = &reply_ph->eth;

		} else {
			void *data;

			dev_kfree_skb(skb);

 linear:
			/* failed to append, revert back to copy into a linear skb */
			omx_counter_inc(iface, PULL_REPLY_SEND_LINEAR);
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
			reply_mh = omx_skb_mac_header(skb);
			reply_ph = &reply_mh->head;
			reply_eh = &reply_ph->eth;
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
		OMX_HTON_32(pull_reply_n->msg_offset, current_msg_offset);
		OMX_HTON_8(pull_reply_n->frame_seqnum, current_frame_seqnum);
		OMX_HTON_16(pull_reply_n->frame_length, frame_length);
		OMX_HTON_8(pull_reply_n->ptype, OMX_PKT_TYPE_PULL_REPLY);
		OMX_HTON_32(pull_reply_n->dst_pull_handle, src_pull_handle);
		OMX_HTON_32(pull_reply_n->dst_magic, src_magic);

		omx_send_dprintk(reply_eh, "PULL REPLY #%d handle %lx magic %lx frame seqnum %ld length %ld offset %ld", i,
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
xen_out:
	omx_endpoint_release(endpoint);
	dev_kfree_skb(orig_skb);
	goto real_out;

 out_with_region:
	/* release the main reference on the region */
	omx_user_region_release(region);
 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(orig_skb);
real_out:
	dprintk_out();
	TIMER_STOP(&t_pull_request);
	return err;
}

#ifdef OMX_HAVE_DMA_ENGINE

/****************************
 * DMA Copy for pull replies
 */

/*
 * Submit an DMA-offloaded copy if possible, and return the non-copied length if any.
 * Acquires a DMA channel first, if needed. And release it if not needed.
 *
 * Called with the handle locked
 */
static INLINE int
omx_pull_handle_reply_try_dma_copy(struct omx_iface *iface, struct omx_pull_handle *handle,
				   struct sk_buff *skb, uint32_t regoff, uint32_t length)
{
	int remaining_copy = length;
	int acquired_chan = 0;
	struct dma_chan *dma_chan = handle->dma_copy_chan;

	dprintk_in();
	if (unlikely(!dma_chan)) {
		dma_chan = handle->dma_copy_chan = omx_dma_chan_get();
		acquired_chan = 1;
	}

	if (likely(dma_chan)) {
		dma_cookie_t dma_cookie = -1;

		remaining_copy = omx_dma_skb_copy_datagram_to_user_region(dma_chan, &dma_cookie, skb, handle->region, regoff, length);

		if (unlikely(remaining_copy)) {
			printk(KERN_INFO "Open-MX: DMA copy of pull reply partially submitted, %d/%d remaining\n",
			       remaining_copy, (unsigned) length);
			omx_counter_inc(iface, DMARECV_PARTIAL_PULL_REPLY);
		} else {
			omx_counter_inc(iface, DMARECV_PULL_REPLY);
		}

		dprintk(DMA, "skb %p got cookie %d\n", skb, dma_cookie);

		if (likely(dma_cookie > 0)) {
			handle->dma_copy_last_cookie = dma_cookie;
			skb->dma_cookie = dma_cookie;
			__skb_queue_tail(&handle->dma_copy_skb_queue, skb);

		} else if (acquired_chan) {
			/* release the acquired channel, we didn't use it */
			omx_dma_chan_put(dma_chan);
			handle->dma_copy_chan = NULL;
		}
	}

	dprintk_out();
	return remaining_copy;
}

/*
 * Polls DMA hardware and completes the queued skbs accordingly.
 * Lets the caller purge the queue if everything is already complete,
 * or just cleanup the queue a bit.
 *
 * Called with the handle locked
 */
static INLINE enum dma_status
omx__pull_handle_poll_dma_completions(struct dma_chan *dma_chan, dma_cookie_t last, struct sk_buff_head *queue)
{
	dma_cookie_t done, used;
	enum dma_status status;
	struct sk_buff *oldskb;

	dprintk_in();
	dprintk(DMA, "waiting for cookie %d\n", last);

	status = dma_async_memcpy_complete(dma_chan, last, &done, &used);
	if (status != DMA_IN_PROGRESS) {
		BUG_ON(status != DMA_SUCCESS);
		return DMA_SUCCESS;
	}

	dprintk(DMA, "last cookie still in progress (done %d used %d), cleaning up to %d\n",
		done, used, done);

	/* do partial cleanup of dma_skb_queue */
	while ((oldskb = skb_peek(queue)) &&
	       (dma_async_is_complete(oldskb->dma_cookie, done, used) == DMA_SUCCESS)) {
		dprintk(DMA, "cleaning skb %p with cookie %d\n", oldskb, oldskb->dma_cookie);
		__skb_dequeue(queue);
		dev_kfree_skb(oldskb);
	}

	dprintk_out();
	return DMA_IN_PROGRESS;
}

/*
 * Do a round of polling to release some already offload-copied skbs.
 * Release resources if everything is already done.
 *
 * Called with the handle locked
 */
static void
omx_pull_handle_poll_dma_completions(struct omx_pull_handle *handle)
{
	struct dma_chan *dma_chan;

	dprintk_in();
	dma_chan = handle->dma_copy_chan;
	if (unlikely(!dma_chan))
		goto out;

	/* Push remaining copies to the DMA hardware */
	dma_async_memcpy_issue_pending(dma_chan);

	if (omx__pull_handle_poll_dma_completions(dma_chan, handle->dma_copy_last_cookie, &handle->dma_copy_skb_queue)
	    == DMA_SUCCESS) {
		/* All copies are already done, it's safe to free early-copied skbs now */
		dprintk(DMA, "all cookies are ready\n");
		__skb_queue_purge(&handle->dma_copy_skb_queue);
		omx_dma_chan_put(dma_chan);
		handle->dma_copy_chan = NULL;
		handle->dma_copy_last_cookie = -1;
	}
out:
	dprintk_out();
	return;
}

/*
 * Wait until all DMA-offloaded copies for this handle are completed,
 * and release the resources.
 *
 * Called with the handle locked
 */
static void
omx_pull_handle_wait_dma_completions(struct omx_pull_handle *handle)
{
	struct dma_chan *dma_chan;

	dprintk_in();
	dma_chan = handle->dma_copy_chan;
	if (unlikely(!dma_chan))
		goto out;

	/* Push remaining copies to the DMA hardware */
	dma_async_memcpy_issue_pending(dma_chan);

	while (omx__pull_handle_poll_dma_completions(dma_chan, handle->dma_copy_last_cookie, &handle->dma_copy_skb_queue) == DMA_IN_PROGRESS);

	/* All copies already done, it's safe to free early-copied skbs now */
	dprintk(DMA, "all cookies are ready\n");
	__skb_queue_purge(&handle->dma_copy_skb_queue);
	omx_dma_chan_put(dma_chan);
	handle->dma_copy_chan = NULL;
	handle->dma_copy_last_cookie = -1;
out:
	dprintk_out();
	return;
}

/*
 * Deferred wait for completions work.
 */
static void
omx_pull_handle_deferred_dma_completions_wait_workfunc(omx_work_struct_data_t data)
{
	struct omx_pull_handle *handle = OMX_WORK_STRUCT_DATA(data, struct omx_pull_handle, dma_copy_deferred_wait_work);

	dprintk_in();
	/* let the offloaded copy finish */
	omx_pull_handle_wait_dma_completions(handle);

	/* complete the handle for real now */
	omx_pull_handle_notify(handle);
	dprintk_out();
}

/*
 * Check whether all DMA-offloaded copies for this handle are completed.
 * If so, return 0;
 * If not, schedule a work to wait for the completion and return -EAGAIN.
 */
static int
omx_pull_handle_deferred_wait_dma_completions(struct omx_pull_handle *handle)
{
	int ret = 0;
	dprintk_in();
	/* do a round of completion */
	omx_pull_handle_poll_dma_completions(handle);

	/* if still something to do, schedule a deferred work */
	if (likely(handle->dma_copy_chan)) {
		schedule_work(&handle->dma_copy_deferred_wait_work);
		omx_counter_inc(handle->endpoint->iface, DMARECV_PULL_REPLY_WAIT_DEFERRED);
		ret = -EAGAIN;
	}

	dprintk_out();
	return ret;
}

#endif /* !OMX_HAVE_DMA_ENGINE */

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
					    int idesc)
{
	struct sk_buff * skb, * skbs[] = { [0 ... OMX_PULL_BLOCK_DESCS_NR-1] = NULL };
	int completed_block = !handle->block_desc[idesc].frames_missing_bitmap;
	int i;

	dprintk_in();
	/* tell the sparse checker that the lock has been taken by the caller */
	__acquire(&handle->lock);

	if (handle->block_desc[0].frames_missing_bitmap) {
		/*
		 * current first block not done, we basically just need to release the handle
		 */

		if (completed_block && idesc > 0 && handle->already_rerequested_blocks < idesc) {

			/* a later block is done without the first ones,
			 * we assume some packet got lost in the first ones,
			 * so we request the first ones again
			 */

			omx_counter_inc(iface, PULL_NONFIRST_BLOCK_DONE_EARLY);

			dprintk(PULL, "pull handle %p second block done without first, requesting first block again\n",
				handle);

			for(i=handle->already_rerequested_blocks; i<idesc; i++) {
				if (handle->block_desc[i].frames_missing_bitmap) {
					skb = omx_fill_pull_block_request(handle, i);
					if (unlikely(IS_ERR(skb))) {
						BUG_ON(PTR_ERR(skb) != -ENOMEM);
						goto skbs_ready; /* don't try to submit more */
					} else {
						skbs[i] = skb;
						handle->already_rerequested_blocks = i+1;
					}
				}
			}
		}

	} else {
		/*
		 * current first block request is done
		 */

		int first_block;

		omx_pull_handle_first_block_done(handle);
		/* drop next blocks if they are done */
		for(i=1; i<OMX_PULL_BLOCK_DESCS_NR; i++) {
			if (!handle->nr_valid_block_descs
			    || handle->block_desc[0].frames_missing_bitmap)
				break;
			omx_pull_handle_first_block_done(handle);
		}
		first_block = handle->nr_valid_block_descs;

		/* prepare as many new blocks as needed */
		while (handle->nr_valid_block_descs < OMX_PULL_BLOCK_DESCS_NR
		       && handle->remaining_length) {
			uint32_t block_length;
			/* prepare the next block */
			block_length = OMX_PULL_BLOCK_LENGTH_MAX;
			if (block_length > handle->remaining_length)
				block_length = handle->remaining_length;
			omx_pull_handle_append_needed_frames(handle, block_length, 0);
		}

		if (handle->nr_valid_block_descs - first_block > 1)
			omx_counter_inc(iface, PULL_REQUEST_NOTONLYFIRST_BLOCKS);

		/* try to actually request the needed new blocks */
		for(i=first_block; i<handle->nr_valid_block_descs; i++) {
			if (i > first_block)
				dprintk(PULL, "queueing another next pull block request\n");
			else
				dprintk(PULL, "queueing next pull block request\n");

			skb = omx_fill_pull_block_request(handle, i);
			if (unlikely(IS_ERR(skb))) {
				BUG_ON(PTR_ERR(skb) != -ENOMEM);
				/* let the timeout expire and resend */
				goto skbs_ready;
			} else {
				skbs[i] = skb;
			}
		}
	}

 skbs_ready:
	if (completed_block) {
		/* cleanup a bit of dma-offloaded copies */
		omx_pull_handle_poll_dma_completions(handle);
	}

	/* reschedule the timeout handler now that we are ready to send the requests */
	/* timer still pending, use the mod_timer_pending() */
	omx_mod_timer_pending(&handle->retransmit_timer,
			      get_jiffies_64() + OMX_PULL_RETRANSMIT_TIMEOUT_JIFFIES);

	/*
	 * do not keep the lock while sending
	 * since the loopback device may cause reentrancy
	 */
	spin_unlock(&handle->lock);

	for(i=0; i<OMX_PULL_BLOCK_DESCS_NR; i++)
		if (likely(skbs[i]))
			omx_queue_xmit(iface, skbs[i], PULL_REQ);

	dprintk_out();
}

int
omx_recv_pull_reply(struct omx_iface * iface,
		    struct omx_hdr * mh,
		    struct sk_buff * skb)
{
	struct omx_pkt_pull_reply *pull_reply_n = &mh->body.pull_reply;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	uint32_t dst_pull_handle = OMX_NTOH_32(pull_reply_n->dst_pull_handle);
	uint32_t dst_magic = OMX_NTOH_32(pull_reply_n->dst_magic);
	uint32_t frame_length = OMX_NTOH_16(pull_reply_n->frame_length);
	uint32_t frame_seqnum = OMX_NTOH_8(pull_reply_n->frame_seqnum);
	uint32_t msg_offset = OMX_NTOH_32(pull_reply_n->msg_offset);
	uint32_t frame_seqnum_offset; /* unsigned to make seqnum offset easy to check */
	int idesc;
	struct omx_endpoint * endpoint;
	struct omx_pull_handle * handle;
	omx_block_frame_bitmask_t bitmap_mask;
	int remaining_copy = frame_length;
	int err = 0;
	int free_skb = 1;

	dprintk_in();
	TIMER_START(&t_pull_reply);
	omx_counter_inc(iface, RECV_PULL_REPLY);

	omx_recv_dprintk(&mh->head.eth, "PULL REPLY handle %lx magic %lx frame seqnum %ld length %ld skb length %ld",
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
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_magic ^ OMX_ENDPOINT_PULL_MAGIC_XOR);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with bad endpoint index within magic %ld",
				 (unsigned long) dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out;
	}

	/* acquire the handle within the endpoint slot array */
	handle = omx_pull_handle_acquire_from_slot(endpoint, dst_pull_handle);
	if (unlikely(!handle)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_WIRE_HANDLE);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with bad wire handle %lx",
				 (unsigned long) dst_pull_handle);
		/* no need to nack this */
		err = -EINVAL;
		goto out_with_endpoint;
	}

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

	/*
	 * compute the frame seqnum offset:
	 * frame_seqnum is already %256, so do the same for h->frame_index, compute the difference
	 * and make sure %256 returns something>0 by adding another 256
	 */
	frame_seqnum_offset = (frame_seqnum - (handle->frame_index % 256) + 256) % 256;

	/* check that the frame seqnum is correct for this msg offset */
        if (unlikely((msg_offset+OMX_PULL_REPLY_LENGTH_MAX-1) / OMX_PULL_REPLY_LENGTH_MAX != handle->frame_index + frame_seqnum_offset)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_SEQNUM_WRAPAROUND);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with invalid seqnum %ld (offset %ld), should be %ld (msg offset %ld)",
				 (unsigned long) frame_seqnum,
				 (unsigned long) frame_seqnum_offset,
				 (unsigned long) (msg_offset+OMX_PULL_REPLY_LENGTH_MAX-1) / OMX_PULL_REPLY_LENGTH_MAX,
				 (unsigned long) msg_offset);
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	/* check that the frame is from this block, and handle wrap around 256 */
	if (unlikely(frame_seqnum_offset >= handle->nr_requested_frames)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_SEQNUM);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with invalid seqnum %ld (offset %ld), should be within %ld-%ld",
				 (unsigned long) frame_seqnum,
				 (unsigned long) frame_seqnum_offset,
				 (unsigned long) handle->frame_index,
				 (unsigned long) handle->frame_index + handle->nr_requested_frames);
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	/* check that the frame is not a duplicate */
	idesc = frame_seqnum_offset / OMX_PULL_REPLY_PER_BLOCK;
	bitmap_mask = ((omx_block_frame_bitmask_t) 1) << (frame_seqnum_offset % OMX_PULL_REPLY_PER_BLOCK);
	if (unlikely((handle->block_desc[idesc].frames_missing_bitmap & bitmap_mask) == 0)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_DUPLICATE);
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet with duplicate seqnum %ld (offset %ld) in current block %ld-%ld",
				 (unsigned long) frame_seqnum,
				 (unsigned long) frame_seqnum_offset,
				 (unsigned long) handle->frame_index,
				 (unsigned long) handle->frame_index + handle->nr_requested_frames);
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}
	handle->block_desc[idesc].frames_missing_bitmap &= ~bitmap_mask;
	handle->nr_missing_frames--;

#if (defined OMX_HAVE_DMA_ENGINE) && !(defined OMX_NORECVCOPY)
	if (omx_dmaengine
	    && frame_length >= omx_dma_async_frag_min
	    && handle->total_length >= omx_dma_async_min) {
		remaining_copy = omx_pull_handle_reply_try_dma_copy(iface, handle, skb, msg_offset, frame_length);
		if (likely(remaining_copy != frame_length))
			free_skb = 0;
	}
#endif

	/* our copy is pending */
	handle->host_copy_nr_frames++;

	/* request more replies if necessary */
	omx_progress_pull_on_recv_pull_reply_locked(iface, handle, idesc);
	/* tell the sparse checker that the lock has been released by omx_progress_pull_on_recv_pull_reply_locked() */
	__release(&handle->lock);

#ifndef OMX_NORECVCOPY
	if (remaining_copy) {
		/* fill segment pages, if something remains to be copied */
		dprintk(PULL, "copying PULL_REPLY %ld bytes for msg_offset %ld at region offset %ld\n",
		       (unsigned long) frame_length,
		       (unsigned long) msg_offset,
		       (unsigned long) msg_offset);
		err = omx_user_region_fill_pages(handle->region, handle->xregion,
						 msg_offset,
						 skb,
						 frame_length);
		if (unlikely(err < 0)) {
			omx_counter_inc(iface, PULL_REPLY_FILL_FAILED);
			omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet due to failure to fill pages from skb");

			/* the other peer is sending crap, close the handle and report truncated to userspace
			 * we do not really care about what have been tranfered since it's crap
			 */
			spin_lock(&handle->lock);
			omx_pull_handle_mark_completed(handle, OMX_EVT_PULL_DONE_ABORTED);
			/* nobody is going to use this handle, no need to lock anymore */
			spin_unlock(&handle->lock);
			omx_pull_handle_bh_notify(handle);
			goto out;
		}
	}
#endif /* OMX_NORECVCOPY */

	/* take the lock back to prepare to complete */
	spin_lock(&handle->lock);

	/* our copy is done */
	handle->host_copy_nr_frames--;

	/* check the status now that we own the lock */
	if (handle->status != OMX_PULL_HANDLE_STATUS_OK) {
		/* the handle is being closed, forget about this packet */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		err = 0;
		goto out_with_endpoint;
	}

	if (!handle->remaining_length && !handle->nr_missing_frames && !handle->host_copy_nr_frames) {
		/* handle is done, notify the completion */
		dprintk(PULL, "notifying pull completion\n");
		omx_pull_handle_mark_completed(handle, OMX_EVT_PULL_DONE_SUCCESS);
		/* nobody is going to use this handle, no need to lock anymore */
		spin_unlock(&handle->lock);
		omx_pull_handle_bh_notify(handle);
	} else {
		/* there's more to receive or copy, just release the handle */
		spin_unlock(&handle->lock);
		omx_pull_handle_release(handle);
		omx_endpoint_release(endpoint);
	}

	err = 0;
	goto out;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	if (free_skb)
		dev_kfree_skb(skb);
	TIMER_STOP(&t_pull_reply);
	dprintk_out();
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
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_nack_mcp *nack_mcp_n = &mh->body.nack_mcp;
	enum omx_nack_type nack_type = OMX_NTOH_8(nack_mcp_n->nack_type);
	uint32_t dst_pull_handle = OMX_NTOH_32(nack_mcp_n->src_pull_handle);
	uint32_t dst_magic = OMX_NTOH_32(nack_mcp_n->src_magic);
	struct omx_endpoint * endpoint;
	struct omx_pull_handle * handle;
	int err = 0;

	dprintk_in();
	omx_counter_inc(iface, RECV_NACK_MCP);

	omx_recv_dprintk(eh, "NACK MCP type %s",
			 omx_strnacktype(nack_type));

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		/* FIXME: impossible? in non MX-wire compatible only? */
		struct omx_peer *peer;
		uint64_t src_addr;

		if (peer_index != (uint16_t)-1) {
			omx_drop_dprintk(eh, "NACK MCP with bad peer index %d",
					 (unsigned) peer_index);
			goto out;
		}

		src_addr = omx_board_addr_from_ethhdr_src(eh);

		/* RCU section while manipulating peers */
		rcu_read_lock();

		peer = omx_peer_lookup_by_addr_locked(src_addr);
		if (!peer) {
			rcu_read_unlock();
			omx_counter_inc(iface, DROP_BAD_PEER_ADDR);
			omx_drop_dprintk(eh, "NACK MCP packet from unknown peer\n");
			goto out;
		}

		peer_index = peer->index;

		/* end of RCU section while manipulating peers */
		rcu_read_unlock();
	}

	/* acquire the endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_magic ^ OMX_ENDPOINT_PULL_MAGIC_XOR);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_MAGIC_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "NACK MCP packet with bad endpoint index within magic %ld",
				 (unsigned long) dst_magic);
		/* no need to nack this */
		err = -EINVAL;
		goto out;
	}

	/* acquire the handle within the endpoint slot array */
	handle = omx_pull_handle_acquire_from_slot(endpoint, dst_pull_handle);
	if (unlikely(!handle)) {
		omx_counter_inc(iface, DROP_PULL_REPLY_BAD_WIRE_HANDLE);
		omx_drop_dprintk(&mh->head.eth, "NACK MCP packet with bad wire handle %lx",
				 (unsigned long) dst_pull_handle);
		/* no need to nack this */
		err = -EINVAL;
		goto out_with_endpoint;
	}

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
	omx_pull_handle_mark_completed(handle, nack_type);
	/* nobody is going to use this handle, no need to lock anymore */
	spin_unlock(&handle->lock);
	omx_pull_handle_bh_notify(handle);

	err = 0;
	goto out;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	dprintk_out();
	return err;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
