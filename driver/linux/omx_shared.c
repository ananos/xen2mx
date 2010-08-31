/*
 * Open-MX
 * Copyright © INRIA 2007-2010 (see AUTHORS file)
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

#include "omx_endpoint.h"
#include "omx_shared.h"
#include "omx_iface.h"
#include "omx_misc.h"
#include "omx_reg.h"
#include "omx_dma.h"
#include "omx_common.h"
#include "omx_io.h"

/********************
 * Endpoint checking
 */

/*
 * acquire the destination endpoint or return a nack_type
 * if the endpoint isn't available or the session is wrong.
 */
static INLINE struct omx_endpoint *
omx_shared_get_endpoint_or_nack_type(uint16_t dst_peer_index, uint8_t dst_endpoint_index,
				     uint32_t session_id,
				     enum omx_nack_type *nack_type)
{
	struct omx_endpoint * dst_endpoint;

	dst_endpoint = omx_local_peer_acquire_endpoint(dst_peer_index, dst_endpoint_index);

	if (unlikely(!dst_endpoint)) {
		/* the peer isn't local, no need to nack */
		if (nack_type)
			*nack_type = OMX_NACK_TYPE_NONE;
		return NULL;
	}

	if (unlikely(IS_ERR(dst_endpoint))) {
		/* the peer is local but the endpoint is invalid */
		if (nack_type)
			*nack_type = omx_endpoint_acquire_by_iface_index_error_to_nack_type(dst_endpoint);
		return NULL;
	}

	if (unlikely(session_id != dst_endpoint->session_id)) {
		/* the peer is local, the endpoint is valid, but the session id is wrong */
		if (nack_type)
			*nack_type = OMX_NACK_TYPE_BAD_SESSION;
		omx_endpoint_release(dst_endpoint);
		return NULL;
	}

	/* endpoint acquired and session ok */
	return dst_endpoint;
}

static INLINE void
omx_shared_notify_nack(struct omx_endpoint *src_endpoint,
		       uint16_t dst_peer_index, uint8_t dst_endpoint_index, uint16_t seqnum,
		       enum omx_nack_type nack_type)
{
	struct omx_evt_recv_nack_lib event;

	event.peer_index = dst_peer_index;
	event.src_endpoint = dst_endpoint_index;
	event.seqnum = seqnum;
	event.nack_type = nack_type;
	event.type = OMX_EVT_RECV_NACK_LIB;

	/* notify the event */
	omx_notify_unexp_event(src_endpoint, &event, sizeof(event));
	/* ignore errors.
	 * if no more unexpected eventq slot, just drop the packet,
	 * it will be resent anyway
	 */
}

/*
 * acquire the destination endpoint or notify a lib nack
 * if the endpoint isn't available or the session is wrong.
 */
static INLINE struct omx_endpoint *
omx_shared_get_endpoint_or_notify_nack(struct omx_endpoint *src_endpoint,
				       uint16_t dst_peer_index, uint8_t dst_endpoint_index,
				       uint32_t session_id, uint16_t seqnum)
{
	struct omx_endpoint * dst_endpoint;
	enum omx_nack_type nack_type = OMX_NACK_TYPE_NONE;

	dst_endpoint = omx_shared_get_endpoint_or_nack_type(dst_peer_index, dst_endpoint_index,
							    session_id, &nack_type);
	if (likely(dst_endpoint != NULL))
		return dst_endpoint;

	if (nack_type == OMX_NACK_TYPE_NONE)
		/* no need to nack, just report the error */
		return NULL;

	omx_shared_notify_nack(src_endpoint, dst_peer_index, dst_endpoint_index, seqnum, nack_type);
	return NULL;
}

/***********************
 * Main Shared Routines
 */

/*
 * try to acquire an endpoint if its peer is local.
 * returns 0 on success
 * returns 1 if the endpoint isn't local (the caller needs to use the network)
 * returns <0 on real error
 */
int
omx_shared_try_send_connect_request(struct omx_endpoint *src_endpoint,
				    const struct omx_cmd_send_connect_request *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_connect_request event;
	int err;

	dst_endpoint = omx_local_peer_acquire_endpoint(hdr->peer_index, hdr->dest_endpoint);
	if (unlikely(!dst_endpoint))
		/* peer isn't local, return 1 to use the network */
		return 1;

	if (unlikely(IS_ERR(dst_endpoint))) {
		enum omx_nack_type nack_type = omx_endpoint_acquire_by_iface_index_error_to_nack_type(dst_endpoint);
		omx_shared_notify_nack(src_endpoint, hdr->peer_index, hdr->dest_endpoint, hdr->seqnum, nack_type);
		/* peer is local, return a success since we reported the nack already */
		return 0;
	}

	/* no session to check for connect */

	/* feel the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.shared = 1;
	event.seqnum = hdr->seqnum;
	event.src_session_id = hdr->src_session_id;
	event.app_key = hdr->app_key;
	event.target_recv_seqnum_start = hdr->target_recv_seqnum_start;
	event.connect_seqnum = hdr->connect_seqnum;
	event.type = OMX_EVT_RECV_CONNECT_REQUEST;

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_CONNECT_REQUEST);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_try_send_connect_reply(struct omx_endpoint *src_endpoint,
				  const struct omx_cmd_send_connect_reply *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_connect_reply event;
	int err;

	dst_endpoint = omx_local_peer_acquire_endpoint(hdr->peer_index, hdr->dest_endpoint);
	if (unlikely(!dst_endpoint))
		/* peer isn't local, return 1 to use the network */
		return 1;

	if (unlikely(IS_ERR(dst_endpoint))) {
		enum omx_nack_type nack_type = omx_endpoint_acquire_by_iface_index_error_to_nack_type(dst_endpoint);
		omx_shared_notify_nack(src_endpoint, hdr->peer_index, hdr->dest_endpoint, hdr->seqnum, nack_type);
		/* peer is local, return a success since we reported the nack already */
		return 0;
	}

	/* no session to check for connect */

	/* feel the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.shared = 1;
	event.seqnum = hdr->seqnum;
	event.src_session_id = hdr->src_session_id;
	event.target_session_id = hdr->target_session_id;
	event.target_recv_seqnum_start = hdr->target_recv_seqnum_start;
	event.connect_seqnum = hdr->connect_seqnum;
	event.connect_status_code = hdr->connect_status_code;
	event.type = OMX_EVT_RECV_CONNECT_REPLY;

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_CONNECT_REPLY);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_send_tiny(struct omx_endpoint *src_endpoint,
		     const struct omx_cmd_send_tiny_hdr *hdr, const void __user * data)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_msg event;
	int length = hdr->length;
	int err;

	BUG_ON(length > OMX_TINY_MSG_LENGTH_MAX); /* required to shutup gcc 4.4 copy_from_user size checks in 2.6.33/x86_32 */

	dst_endpoint = omx_shared_get_endpoint_or_notify_nack(src_endpoint, hdr->peer_index,
							      hdr->dest_endpoint, hdr->session_id,
							      hdr->seqnum);
	if (unlikely(!dst_endpoint))
		return 0;

	/* fill the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.tiny.length = hdr->length;
	event.specific.tiny.checksum = hdr->checksum;
	event.type = OMX_EVT_RECV_TINY;

#ifndef OMX_NORECVCOPY
	/* copy the data */
	err = copy_from_user(&event.specific.tiny.data, data, length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send tiny cmd data\n");
		err = -EFAULT;
		goto out_with_endpoint;
	}
#endif

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_TINY);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_send_small(struct omx_endpoint *src_endpoint,
		      const struct omx_cmd_send_small *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_msg event;
	unsigned long recvq_offset;
	int err;

	dst_endpoint = omx_shared_get_endpoint_or_notify_nack(src_endpoint, hdr->peer_index,
							      hdr->dest_endpoint, hdr->session_id,
							      hdr->seqnum);
	if (unlikely(!dst_endpoint))
		return 0;

	/* get the eventq slot */
	err = omx_prepare_notify_unexp_event_with_recvq(dst_endpoint, &recvq_offset);
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}

#ifndef OMX_NORECVCOPY
	/* copy the data */
	err = copy_from_user(dst_endpoint->recvq + recvq_offset,
			     (__user void *)(unsigned long) hdr->vaddr,
			     hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send small cmd data\n");
		err = -EFAULT;
		goto out_with_dst_event;
	}
#endif

	/* fill and notify the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.small.length = hdr->length;
	event.specific.small.recvq_offset = recvq_offset;
	event.specific.small.checksum = hdr->checksum;
	event.type = OMX_EVT_RECV_SMALL;
	omx_commit_notify_unexp_event_with_recvq(dst_endpoint, &event, sizeof(event));
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_SMALL);

	return 0;

 out_with_dst_event:
	omx_cancel_notify_unexp_event_with_recvq(dst_endpoint);
 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_send_mediumsq_frag(struct omx_endpoint *src_endpoint,
			      const struct omx_cmd_send_mediumsq_frag *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_msg dst_event;
	struct omx_evt_send_mediumsq_frag_done src_event;
	unsigned long recvq_offset;
#ifdef OMX_HAVE_DMA_ENGINE
	dma_cookie_t dma_cookie = -1;
	struct dma_chan *dma_chan = NULL;
#endif
	int frag_length = hdr->frag_length;
	int sendq_offset = hdr->sendq_offset;
	int remaining;
	int current_recvq_offset;
	int current_sendq_offset;
	int err;

	dst_endpoint = omx_shared_get_endpoint_or_notify_nack(src_endpoint, hdr->peer_index,
							      hdr->dest_endpoint, hdr->session_id,
							      hdr->seqnum);
	if (unlikely(!dst_endpoint))
		return 0;

	/* get the dst eventq slot */
	err = omx_prepare_notify_unexp_event_with_recvq(dst_endpoint, &recvq_offset);
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}

#ifndef OMX_NORECVCOPY
	/* copy the data */
	current_sendq_offset = sendq_offset;
	current_recvq_offset = recvq_offset;
	remaining = frag_length;
#ifdef OMX_HAVE_DMA_ENGINE
	if (omx_dmaengine && frag_length >= omx_dma_sync_min)
		dma_chan = omx_dma_chan_get();
	if (dma_chan) {

		while (remaining) {
			dma_cookie_t new_cookie;
			int chunk = remaining;
			if (chunk > PAGE_SIZE)
				chunk = PAGE_SIZE;
			new_cookie = dma_async_memcpy_pg_to_pg(dma_chan,
							       dst_endpoint->recvq_pages[current_recvq_offset >> PAGE_SHIFT],
							       current_recvq_offset & (~PAGE_MASK),
							       src_endpoint->sendq_pages[current_sendq_offset >> PAGE_SHIFT],
							       current_sendq_offset & (~PAGE_MASK),
							       chunk);
			if (new_cookie < 0)
				break;
			dma_cookie = new_cookie;
			remaining -= chunk;
			current_sendq_offset += chunk;
			current_recvq_offset += chunk;
		}

		if (dma_cookie > 0)
			dma_async_memcpy_issue_pending(dma_chan);
	}
#endif
	if (remaining) {
		memcpy(dst_endpoint->recvq + current_recvq_offset,
		       src_endpoint->sendq + current_sendq_offset,
		       remaining);
	}
#endif

	/* fill the dst event */
	dst_event.peer_index = src_endpoint->iface->peer.index;
	dst_event.src_endpoint = src_endpoint->endpoint_index;
	dst_event.match_info = hdr->match_info;
	dst_event.seqnum = hdr->seqnum;
	dst_event.piggyack = hdr->piggyack;
	dst_event.specific.medium_frag.msg_length = hdr->msg_length;
	dst_event.specific.medium_frag.frag_length = frag_length;
	dst_event.specific.medium_frag.frag_seqnum = hdr->frag_seqnum;
	dst_event.specific.medium_frag.frag_pipeline = hdr->frag_pipeline;
	dst_event.specific.medium_frag.checksum = hdr->checksum;
	dst_event.specific.medium_frag.recvq_offset = recvq_offset;
	dst_event.type = OMX_EVT_RECV_MEDIUM_FRAG;

	/* make sure the copy is done */
#ifdef OMX_HAVE_DMA_ENGINE
	if (dma_chan) {
		if (dma_cookie > 0) {
			while (dma_async_memcpy_complete(dma_chan, dma_cookie, NULL, NULL) == DMA_IN_PROGRESS);
			omx_counter_inc(omx_shared_fake_iface, SHARED_DMA_MEDIUM_FRAG);
		}
		omx_dma_chan_put(dma_chan);
	}
#endif

	/* notify the dst event */
	omx_commit_notify_unexp_event_with_recvq(dst_endpoint, &dst_event, sizeof(dst_event));

	/* fill and notify the src event */
	src_event.sendq_offset = hdr->sendq_offset;
	src_event.type = OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE;
	omx_notify_exp_event(src_endpoint, &src_event, sizeof(src_event));
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_MEDIUMSQ_FRAG);

	return 0;

 out_with_endpoint:
	/* fill and notify the src event anyway, so that the sender doesn't leak eventq slots */
	src_event.sendq_offset = hdr->sendq_offset;
	src_event.type = OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE;
	omx_notify_exp_event(src_endpoint, &src_event, sizeof(src_event));

	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_send_mediumva(struct omx_endpoint *src_endpoint,
			 const struct omx_cmd_send_mediumva *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_msg dst_event;
	struct omx_cmd_user_segment *usegs, *cur_useg;
	uint32_t msg_length, remaining, cur_useg_remaining;
	void __user * cur_udata;
	unsigned long *recvq_offset;
	uint32_t nseg;
	int ret;
	int frags_nr;
     	int i;

	msg_length = hdr->length;

	recvq_offset = kmalloc((msg_length >> OMX_RECVQ_ENTRY_SHIFT) * sizeof(*recvq_offset), GFP_KERNEL);
	if (!recvq_offset)
		return -ENOMEM;

	dst_endpoint = omx_shared_get_endpoint_or_notify_nack(src_endpoint, hdr->peer_index,
							      hdr->dest_endpoint, hdr->session_id,
							      hdr->seqnum);
	if (unlikely(!dst_endpoint)) {
		ret = 0;
		goto out_with_recvq_offset;
	}

	nseg = hdr->nr_segments;
	frags_nr = (msg_length + OMX_RECVQ_ENTRY_SIZE - 1) >> OMX_RECVQ_ENTRY_SHIFT;
	/* FIXME: assert <= 8 */

	/* get user segments */
	usegs = kmalloc(nseg * sizeof(struct omx_cmd_user_segment), GFP_KERNEL);
	if ( !usegs) {
		printk(KERN_ERR "Open-MX: Cannot allocate segments for mediumva\n");
		ret = -ENOMEM;
		goto out_with_endpoint;
	}
	ret = copy_from_user(usegs, (void __user *)(unsigned long) hdr->segments,
			     nseg * sizeof(struct omx_cmd_user_segment));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read mediumva segments cmd\n");
		ret = -EFAULT;
		goto out_with_usegs;
	}

	/* compute the segments length */
	remaining = 0;
	for(i=0; i<nseg; i++)
		remaining += usegs[i].len;
	if (remaining != msg_length) {
		printk(KERN_ERR "Open-MX: Cannot send mediumva without enough data in segments (%ld instead of %ld)\n",
		       (unsigned long) remaining, (unsigned long) msg_length);
		ret = -EINVAL;
		goto out_with_usegs;
	}

	/* initialize position in segments */
	cur_useg = &usegs[0];
	cur_useg_remaining = cur_useg->len;
	cur_udata = (__user void *)(unsigned long) cur_useg->vaddr;

	/* get the dst eventq slot */
	ret = omx_prepare_notify_unexp_events_with_recvq(dst_endpoint, frags_nr,
							 recvq_offset);
	if (unlikely(ret < 0)) {
		/* no more unexpected eventq slot? just drop the message, it will be resent anyway */
		ret = 0;
		goto out_with_usegs;
	}

	/* fill the dst event */
	dst_event.peer_index = src_endpoint->iface->peer.index;
	dst_event.src_endpoint = src_endpoint->endpoint_index;
	dst_event.match_info = hdr->match_info;
	dst_event.seqnum = hdr->seqnum;
	dst_event.piggyack = hdr->piggyack;
	dst_event.specific.medium_frag.msg_length = hdr->length;
	dst_event.specific.medium_frag.checksum = hdr->checksum;
	dst_event.specific.medium_frag.frag_pipeline = OMX_RECVQ_ENTRY_SHIFT;

#ifndef OMX_NORECVCOPY
	/* initialize position in segments */
	cur_useg = &usegs[0];
	cur_useg_remaining = cur_useg->len;
	cur_udata = (__user void *)(unsigned long) cur_useg->vaddr;

	for(i=0; i<frags_nr; i++) {
		uint16_t frag_length = remaining > OMX_RECVQ_ENTRY_SIZE ? OMX_RECVQ_ENTRY_SIZE : remaining;
		uint16_t frag_remaining = frag_length;
		void *cur_dest = dst_endpoint->recvq + recvq_offset[i];

		/* copy the data right after the header */
		while (frag_remaining) {
			uint16_t chunk = frag_remaining > cur_useg_remaining ? cur_useg_remaining : frag_remaining;

			ret = copy_from_user(cur_dest, cur_udata, chunk);
			if (unlikely(ret != 0)) {
				printk(KERN_ERR "Open-MX: Failed to read send mediumva cmd data\n");
				ret = -EFAULT;
				goto out_with_events;
			}

			if (chunk == cur_useg_remaining) {
				cur_useg++;
				cur_udata = (__user void *)(unsigned long) cur_useg->vaddr;
				cur_useg_remaining = cur_useg->len;
			} else {
				cur_udata += chunk;
				cur_useg_remaining -= chunk;
			}
			frag_remaining -= chunk;
			cur_dest += chunk;
		}
		remaining -= frag_length;
	}
#endif

	remaining = msg_length;
	for(i=0; i<frags_nr; i++) {
		uint16_t frag_length = remaining > OMX_RECVQ_ENTRY_SIZE ? OMX_RECVQ_ENTRY_SIZE : remaining;
		/* notify the dst event */
		dst_event.specific.medium_frag.frag_length = frag_length;
		dst_event.specific.medium_frag.frag_seqnum = i;
		dst_event.specific.medium_frag.recvq_offset = recvq_offset[i];
		dst_event.type = OMX_EVT_RECV_MEDIUM_FRAG;
		omx_commit_notify_unexp_event_with_recvq(dst_endpoint, &dst_event, sizeof(dst_event));
		remaining -= frag_length;
	}

	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_MEDIUMVA);

	kfree(recvq_offset);
	return 0;

 out_with_events:
	for(i=0; i<frags_nr; i++)
		omx_cancel_notify_unexp_event_with_recvq(dst_endpoint);
 out_with_usegs:
	kfree(usegs);
 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
 out_with_recvq_offset:
	kfree(recvq_offset);
	return ret;
}

int
omx_shared_send_rndv(struct omx_endpoint *src_endpoint,
		     const struct omx_cmd_send_rndv *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_msg event;
	struct omx_user_region * src_region = NULL;
	struct omx_user_region_pin_state pinstate;
	int err;

	dst_endpoint = omx_shared_get_endpoint_or_notify_nack(src_endpoint, hdr->peer_index,
							      hdr->dest_endpoint, hdr->session_id,
							      hdr->seqnum);
	if (unlikely(!dst_endpoint))
		return 0;

	/* fill the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.rndv.msg_length = hdr->msg_length;
	event.specific.rndv.pulled_rdma_id = hdr->pulled_rdma_id;
	event.specific.rndv.pulled_rdma_seqnum = hdr->pulled_rdma_seqnum;
	event.specific.rndv.pulled_rdma_offset = 0; /* not needed in Open-MX */
	event.specific.rndv.checksum = hdr->checksum;
	event.type = OMX_EVT_RECV_RNDV;

	/* make sure the region is marked as pinning before reporting the event */
	if (!omx_pin_synchronous) {
		src_region = omx_user_region_acquire(src_endpoint, hdr->pulled_rdma_id);
		if (unlikely(!src_region)) {
			err = -EINVAL;
			goto out_with_endpoint;
		}

		omx_user_region_demand_pin_init(&pinstate, src_region);
		if (!omx_pin_progressive) {
			/* pin the whole region now */
			pinstate.next_chunk_pages = omx_pin_chunk_pages_max;
			err = omx_user_region_demand_pin_finish(&pinstate);
			omx_user_region_release(src_region);
			src_region = NULL;
			if (err < 0)
				goto out_with_endpoint;
		}
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_region;
	}
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_RNDV);

	if (src_region) {
		/* make sure the region is getting pinned now */
		omx_user_region_demand_pin_finish(&pinstate);
		/* ignore errors, the rndv is gone anyway,
		 * the pull will be aborted
		 */
		omx_user_region_release(src_region);
	}

	return 0;

 out_with_region:
	if (src_region) {
		/* make sure the region is getting pinned anyway */
		pinstate.next_chunk_pages = omx_pin_chunk_pages_max;
		omx_user_region_demand_pin_finish(&pinstate);
		/* ignore errors, the rndv is gone anyway,
		 * the pull will be aborted
		 */
		omx_user_region_release(src_region);
	}
 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_pull(struct omx_endpoint *src_endpoint,
		const struct omx_cmd_pull *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_pull_done event;
	struct omx_user_region *src_region, *dst_region = NULL;
	enum omx_nack_type nack_type = OMX_NACK_TYPE_NONE;
	int err;

	/* get our region */
	src_region = omx_user_region_acquire(src_endpoint, hdr->puller_rdma_id);
	if (!src_region) {
		/* source region is invalid, return an immediate error */
		err = -EINVAL;
		goto out;
	}

	dst_endpoint = omx_shared_get_endpoint_or_nack_type(hdr->peer_index, hdr->dest_endpoint,
							    hdr->session_id, &nack_type);
	if (unlikely(dst_endpoint == NULL)) {
		if (nack_type == OMX_NACK_TYPE_NONE) {
			/* peer invalid, we cannot reach it, assume it's a timeout */
			event.status = OMX_EVT_PULL_DONE_TIMEOUT;
		} else {
			/* dest endpoint invalid, return a pull done status error */
			event.status = nack_type;
		}
		goto out_notify_nack;
	}

	dst_region = omx_user_region_acquire(dst_endpoint, hdr->pulled_rdma_id);
	if (unlikely(dst_region == NULL)) {
		/* dest region invalid, return a pull done status error */
		event.status = OMX_EVT_PULL_DONE_BAD_RDMAWIN;
		goto out_notify_nack_with_dst_endpoint;
	}

#ifndef OMX_NORECVCOPY
	/* pull from the dst region into the src region */
	err = omx_copy_between_user_regions(dst_region, hdr->pulled_rdma_offset,
					    src_region, 0,
					    hdr->length);
	event.status = err < 0 ? OMX_EVT_PULL_DONE_ABORTED : OMX_EVT_PULL_DONE_SUCCESS;
#else
	event.status = OMX_EVT_PULL_DONE_SUCCESS;
#endif

	/* release stuff */
	omx_user_region_release(dst_region);
	omx_endpoint_release(dst_endpoint);
	omx_user_region_release(src_region);

	/* fill and notify the event */
	event.lib_cookie = hdr->lib_cookie;
	event.puller_rdma_id = hdr->puller_rdma_id;
	event.type = OMX_EVT_PULL_DONE;
	omx_notify_exp_event(src_endpoint, &event, sizeof(event));

	omx_counter_inc(omx_shared_fake_iface, SHARED_PULL);

	return 0;

 out_notify_nack_with_dst_endpoint:
	omx_endpoint_release(dst_endpoint);
 out_notify_nack:
	omx_user_region_release(src_region);

	event.lib_cookie = hdr->lib_cookie;
	event.puller_rdma_id = hdr->puller_rdma_id;
	event.type = OMX_EVT_PULL_DONE;
	omx_notify_exp_event(src_endpoint, &event, sizeof(event));
	return 0;

 out:
	return err;
}

int
omx_shared_send_notify(struct omx_endpoint *src_endpoint,
		       const struct omx_cmd_send_notify *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_msg event;
	int err;

	dst_endpoint = omx_shared_get_endpoint_or_notify_nack(src_endpoint, hdr->peer_index,
							      hdr->dest_endpoint, hdr->session_id,
							      hdr->seqnum);
	if (unlikely(!dst_endpoint))
		return 0;

	/* fill the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.notify.length = hdr->total_length;
	event.specific.notify.pulled_rdma_id = hdr->pulled_rdma_id;
	event.specific.notify.pulled_rdma_seqnum = hdr->pulled_rdma_seqnum;
	event.type = OMX_EVT_RECV_NOTIFY;

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_NOTIFY);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_send_liback(struct omx_endpoint *src_endpoint,
		       const struct omx_cmd_send_liback *hdr)
{
	struct omx_endpoint * dst_endpoint;
	struct omx_evt_recv_liback event;
	int err;

	/* don't notify a nack if the endpoint is invalid */
	dst_endpoint = omx_shared_get_endpoint_or_nack_type(hdr->peer_index, hdr->dest_endpoint,
							    hdr->session_id, NULL);
	if (unlikely(!dst_endpoint))
		/* endpoint unreachable, just ignore */
		return 0;

	/* fill the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.acknum = hdr->acknum;
	event.lib_seqnum = hdr->lib_seqnum;
	event.send_seq = hdr->send_seq;
	event.resent = hdr->resent;
	event.type = OMX_EVT_RECV_LIBACK;

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		err = 0;
		goto out_with_endpoint;
	}
	omx_endpoint_release(dst_endpoint);

	omx_counter_inc(omx_shared_fake_iface, SHARED_LIBACK);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(dst_endpoint);
	return err;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
