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
#include <linux/module.h>
#include <linux/slab.h>

#include "omx_common.h"
#include "omx_hal.h"

struct omx_pull_handle {
	struct omx_endpoint * endpoint;
	struct omx_user_region * region;
	uint32_t lib_cookie;
	struct list_head endpoint_pull_handles;
	uint32_t idr_index;

	spinlock_t lock;

	uint32_t length;

	/*
	 * Masks of frames missing (not received at all)
	 * and transferring (received but not copied yet)
	 *
	 * handle is done when frame_transferring = frame_missing = 0
	 * handle is being used when frame_transferring != frame_missing
	 */
	uint32_t frame_missing;
	uint32_t frame_transferring;
	/* FIXME: need a frame window for multiple pull request */
};

/*
 * Notes about locking:
 *
 * A reference is hold on the endpoint while using a pull handle:
 * - when manipulating its internal fields
 *   (by taking the endpoint reference as long as we hold the handle lock)
 * - when copying data corresponding to the handle
 *   (the endpoint reference is hold without taking the handle lock)
 */

/******************************
 * Per-endpoint pull handles management
 */

int
omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint)
{
	spin_lock_init(&endpoint->pull_handle_lock);
	idr_init(&endpoint->pull_handle_idr);
	INIT_LIST_HEAD(&endpoint->pull_handle_list);

	return 0;
}

void
omx_endpoint_pull_handles_exit(struct omx_endpoint * endpoint)
{
	struct omx_pull_handle * handle, * next;

	spin_lock(&endpoint->pull_handle_lock);

	/* release all pull handles of endpoint */
	list_for_each_entry_safe(handle, next,
				 &endpoint->pull_handle_list,
				 endpoint_pull_handles) {
		list_del(&handle->endpoint_pull_handles);
		idr_remove(&endpoint->pull_handle_idr, handle->idr_index);
		kfree(handle);
	}

	spin_unlock(&endpoint->pull_handle_lock);
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

static inline struct omx_endpoint *
omx_endpoint_acquire_by_pull_magic(struct omx_iface * iface, uint32_t magic)
{
	uint32_t full_index;
	uint8_t index;

	full_index = (magic ^ OMX_ENDPOINT_PULL_MAGIC_XOR) >> OMX_ENDPOINT_PULL_MAGIC_SHIFT;
	if (unlikely(full_index & (~0xff)))
		/* index does not fit in 8 bits, drop the packet */
		return NULL;
	index = full_index;

	return omx_endpoint_acquire_by_iface_index(iface, index);
}

/******************************
 * Per-endpoint pull handles create/find/...
 */

/*
 * Create a pull handle and return it as acquired,
 * with a reference on the endpoint
 */
static inline struct omx_pull_handle *
omx_pull_handle_create(struct omx_endpoint * endpoint,
		       uint32_t lib_cookie, uint32_t length, uint32_t rdma_id)
{
	struct omx_pull_handle * handle;
	struct omx_user_region * region;
	int err;

	/* take a reference on the endpoint since we will return the pull_handle as acquired */
	err = omx_endpoint_acquire(endpoint);
	if (unlikely(err < 0))
		goto out;

	/* acquire the region */
	region = omx_user_region_acquire(endpoint, rdma_id);
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
	err = idr_pre_get(&endpoint->pull_handle_idr, GFP_KERNEL);
	if (unlikely(!err)) {
		printk(KERN_ERR "Open-MX: Failed to allocate idr space for pull handles\n");
		err = -ENOMEM;
		goto out_with_handle;
	}

	spin_lock(&endpoint->pull_handle_lock);

	err = idr_get_new(&endpoint->pull_handle_idr, handle, &handle->idr_index);
	if (unlikely(err == -EAGAIN)) {
		spin_unlock(&endpoint->pull_handle_lock);
		printk("omx_pull_handle_create try again\n");
		goto idr_try_alloc;
	}

	/* we are good now, finish filling the handle */
	spin_lock_init(&handle->lock);
	handle->endpoint = endpoint;
	handle->region = region;
	handle->lib_cookie = lib_cookie;
	handle->length = length;
	handle->frame_missing = 0;
	handle->frame_transferring = 0;
	list_add_tail(&handle->endpoint_pull_handles,
		      &endpoint->pull_handle_list);

	/* acquire the handle */
	spin_lock(&handle->lock);

	spin_unlock(&endpoint->pull_handle_lock);

	printk("created and acquired pull handle %p\n", handle);
	return handle;

 out_with_handle:
	kfree(handle);
 out_with_region:
	omx_user_region_release(region);
 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return NULL;
}

/*
 * Acquire a pull handle and the corresponding endpoint
 * given by an pull magic and a wire handle
 */
static inline struct omx_pull_handle *
omx_pull_handle_acquire_by_wire(struct omx_iface * iface,
				uint32_t magic, uint32_t wire_handle)
{
	struct omx_pull_handle * handle;
	struct omx_endpoint * endpoint;

	endpoint = omx_endpoint_acquire_by_pull_magic(iface, magic);
	if (unlikely(!endpoint))
		return NULL;

	spin_lock(&endpoint->pull_handle_lock);
	handle = idr_find(&endpoint->pull_handle_idr, wire_handle);

	/* acquire the handle */
	spin_lock(&handle->lock);

	spin_unlock(&endpoint->pull_handle_lock);

	printk("acquired pull handle %p\n", handle);
	return handle;
}

/*
 * Reacquire a pull handle.
 *
 * A reference is still hold on the endpoint.
 */
static inline void
omx_pull_handle_reacquire(struct omx_pull_handle * handle)
{
	/* acquire the handle */
	spin_lock(&handle->lock);

	printk("reacquired pull handle %p\n", handle);
}

/*
 * Takes a locked pull handle and unlocked it if it is not done yet,
 * or destory it if it is done.
 */
static inline void
omx_pull_handle_release(struct omx_pull_handle * handle)
{
	struct omx_endpoint * endpoint = handle->endpoint;
	struct omx_user_region * region = handle->region;

	printk("releasing pull handle %p\n", handle);

	/* FIXME: add likely/unlikely */
	if (handle->frame_transferring != handle->frame_missing) {
		/* some transfer are pending,
		 * release the handle but keep the reference on the endpoint
		 * since it will be reacquired later
		 */
		spin_unlock(&handle->lock);

		printk("some frames are being transferred, just release the handle\n");

	} else if (handle->frame_transferring != 0) {
		/* no transfer pending but frames are missing,
		 * release the handle and the endpoint
		 */
		spin_unlock(&handle->lock);

		/* release the endpoint */
		omx_endpoint_release(endpoint);

		printk("some frames are missing, release the handle and the endpoint\n");

	} else {
		/* transfer is done,
		 * destroy the handle and release the region and endpoint */

		/* FIXME: if multiple pull requests, start the next one */

		/* destroy the handle */
		spin_lock(&endpoint->pull_handle_lock);
		list_del(&handle->endpoint_pull_handles);
		idr_remove(&endpoint->pull_handle_idr, handle->idr_index);
		kfree(handle);
		spin_unlock(&endpoint->pull_handle_lock);

		/* release the region and endpoint */
		omx_user_region_release(region);
		omx_endpoint_release(endpoint);

		printk("frame are all done, destroy the handle and release the endpoint\n");

	}
}

/******************************
 * Pull-related networking
 */

int
omx_send_pull(struct omx_endpoint * endpoint,
	      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_pull cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct omx_pull_handle * handle;
	struct omx_pkt_pull_request * pull;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send pull cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	handle = omx_pull_handle_create(endpoint,
					cmd.lib_cookie, cmd.length, cmd.local_rdma_id);
	if (unlikely(!handle)) {
		printk(KERN_INFO "Open-MX: Failed to allocate a pull handle\n");
		ret = -ENOMEM;
		goto out;
	}

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(*mh), ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create pull skb\n");
		ret = -ENOMEM;
		goto out_with_handle;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	pull = &mh->body.pull;
	pull->src_endpoint = endpoint->endpoint_index;
	pull->dst_endpoint = cmd.dest_endpoint;
	pull->ptype = OMX_PKT_TYPE_PULL;
	pull->session = cmd.session_id;
	pull->total_length = cmd.length;
	pull->block_length = cmd.length;
	pull->puller_offset = cmd.local_offset;
	pull->pulled_rdma_id = cmd.remote_rdma_id;
	pull->pulled_offset = cmd.remote_offset;
	pull->src_pull_handle = handle->idr_index;
	pull->src_magic = omx_endpoint_pull_magic(endpoint);

	omx_send_dprintk(eh, "PULL handle %lx magic %lx length %ld out of %ld",
			 (unsigned long) pull->src_pull_handle,
			 (unsigned long) pull->src_magic,
			 (unsigned long) pull->block_length,
			 (unsigned long) pull->total_length);

	/* mark the frames as missing and release the handle */
	handle->frame_missing = 1;
	handle->frame_transferring = 1;
	omx_pull_handle_release(handle);

	dev_queue_xmit(skb);

//	printk(KERN_INFO "Open-MX: sent a pull message from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out_with_handle:
	omx_pull_handle_release(handle);
 out:
	return ret;
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
	struct omx_endpoint * endpoint;
	struct ethhdr *pull_eh = &pull_mh->head.eth;
	struct omx_pkt_pull_request *pull_request = &pull_mh->body.pull;
	struct omx_pkt_pull_reply *pull_reply;
	struct sk_buff *skb;
	struct omx_hdr *reply_mh;
	struct ethhdr *reply_eh;
	struct net_device * ifp = iface->eth_ifp;
	struct omx_user_region *region;
	int err = 0;

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, pull_request->dst_endpoint);
	if (unlikely(!endpoint)) {
		omx_drop_dprintk(pull_eh, "PULL packet for unknown endpoint %d",
				 pull_request->dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	/* check the session */
	if (unlikely(pull_request->session != endpoint->session_id)) {
		omx_drop_dprintk(pull_eh, "PULL packet with bad session");
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* alloc the reply skb */
	skb = omx_new_skb(ifp,
			  /* only allocate space for the header now,
			   * we'll attach pages and pad to ETH_ZLEN later
			   */
			  sizeof(*reply_mh));
	if (unlikely(skb == NULL)) {
		omx_drop_dprintk(pull_eh, "PULL packet due to failure to create pull reply skb");
		err = -ENOMEM;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(pull_eh, "PULL handle %lx magic %lx length %ld out of %ld",
			 (unsigned long) pull_request->src_pull_handle,
			 (unsigned long) pull_request->src_magic,
			 (unsigned long) pull_request->block_length,
			 (unsigned long) pull_request->total_length);

	/* locate headers */
	reply_mh = omx_hdr(skb);
	reply_eh = &reply_mh->head.eth;

	/* fill ethernet header */
	memcpy(reply_eh->h_source, ifp->dev_addr, sizeof (reply_eh->h_source));
	reply_eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	/* get the destination address */
	memcpy(reply_eh->h_dest, pull_eh->h_source, sizeof(reply_eh->h_dest));

	/* fill omx header */
	pull_reply = &reply_mh->body.pull_reply;
	pull_reply->puller_offset = pull_request->puller_offset;
	pull_reply->ptype = OMX_PKT_TYPE_PULL_REPLY;
	pull_reply->dst_pull_handle = pull_request->src_pull_handle;
	pull_reply->dst_magic = pull_request->src_magic;

	omx_send_dprintk(reply_eh, "PULL REPLY handle %ld magic %ld",
			 (unsigned long) pull_reply->dst_pull_handle,
			 (unsigned long) pull_reply->dst_magic);

	/* get the rdma window */
	region = omx_user_region_acquire(endpoint, pull_request->pulled_rdma_id);
	if (unlikely(!region))
		goto out_with_skb;

	/* append segment pages */
	err = omx_user_region_append_pages(region,
					   pull_request->pulled_offset,
					   skb,
					   pull_request->block_length);
	if (unlikely(err < 0)) {
		omx_drop_dprintk(pull_eh, "PULL packet due to failure to append pages to skb");
		/* pages will be released in dev_kfree_skb() */
		goto out_with_region;
	}
	pull_reply->length = pull_request->block_length;

 	if (unlikely(skb->len < ETH_ZLEN)) {
		/* pad to ETH_ZLEN */
		err = omx_skb_pad(skb, ETH_ZLEN);
		if (unlikely(err < 0))
			/* skb has been freed in skb_pad') */
			omx_user_region_release(region);
			goto out_with_endpoint;
		skb->len = ETH_ZLEN;
	}

	skb->sk = (void *) region;
	skb->destructor = omx_send_pull_reply_skb_destructor;

	dev_queue_xmit(skb);

	omx_endpoint_release(endpoint);

//	printk(KERN_INFO "Open-MX: sent a pull reply from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out_with_region:
	omx_user_region_release(region);
 out_with_skb:
	dev_kfree_skb(skb);
 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_pull_handle_done_notify(struct omx_pull_handle * handle,
			    uint32_t pulled_length)
{
	struct omx_endpoint *endpoint = handle->endpoint;
	union omx_evt *evt;
	struct omx_evt_pull_done *event;
	int err;

	/* get the eventq slot */
	evt = omx_find_next_eventq_slot(endpoint);
	if (unlikely(!evt)) {
		printk(KERN_INFO "Open-MX: Failed to complete send of PULL packet because of event queue full\n");
		err = -EBUSY;
		goto out;
	}
	event = &evt->pull_done;

	/* fill event */
	event->lib_cookie = handle->lib_cookie;
	event->pulled_length = pulled_length;

	/* set the type at the end so that user-space does not find the slot on error */
	event->type = OMX_EVT_PULL_DONE;

	return 0;

 out:
	return err;
}

int
omx_recv_pull_reply(struct omx_iface * iface,
		    struct omx_hdr * mh,
		    struct sk_buff * skb)
{
	struct omx_pkt_pull_reply *pull_reply = &mh->body.pull_reply;
	struct ethhdr *eh = &mh->head.eth;
	uint32_t pull_length = pull_reply->length;
	struct omx_pull_handle * handle;
	int err = 0;

	omx_recv_dprintk(&mh->head.eth, "PULL REPLY handle %ld magic %ld length %ld skb length %ld",
			 (unsigned long) pull_reply->dst_pull_handle,
			 (unsigned long) pull_reply->dst_magic,
			 (unsigned long) pull_length,
			 (unsigned long) skb->len - sizeof(struct omx_hdr));

	/* check actual data length */
	if (unlikely(pull_length > skb->len - sizeof(struct omx_hdr))) {
		omx_drop_dprintk(eh, "PULL REPLY packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - sizeof(struct omx_hdr),
				 (unsigned) pull_length);
		err = -EINVAL;
		goto out;
	}

	/* acquire the handle and endpoint */
	handle = omx_pull_handle_acquire_by_wire(iface, pull_reply->dst_magic,
						  pull_reply->dst_pull_handle);
	if (unlikely(!handle)) {
		omx_drop_dprintk(&mh->head.eth, "PULL REPLY packet unknown handle %d magic %d",
				 pull_reply->dst_pull_handle, pull_reply->dst_magic);
		err = -EINVAL;
		goto out;
	}

	/* no session to check */

	/* FIXME: store the sender mac in the handle and check it ? */

	handle->frame_missing = 0;

	/* release the handle during the copy */
	omx_pull_handle_release(handle);

	/* fill segment pages */
	err = omx_user_region_fill_pages(handle->region,
					 pull_reply->puller_offset,
					 skb,
					 pull_reply->length);
	if (unlikely(err < 0)) {
		omx_drop_dprintk(eh, "PULL REPLY packet due to failure to fill pages from skb");
		/* the other peer is sending crap, close the handle and report truncated to userspace */
		handle->frame_transferring = 0;
		/* FIXME: make sure a new pull is not queued too, so that the handle is dropped */
		/* FIXME: report what has already been tranferred? */
		omx_pull_handle_done_notify(handle, 0);
		goto out_with_handle;
	}

	/* FIXME: release instead of destroy if not done */
	omx_pull_handle_reacquire(handle);

	handle->frame_transferring = 0;

	if (!handle->frame_transferring)
		omx_pull_handle_done_notify(handle, handle->length);

	omx_pull_handle_release(handle);

	return 0;

 out_with_handle:
	omx_pull_handle_release(handle);
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
