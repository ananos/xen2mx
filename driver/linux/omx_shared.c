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

#include "omx_endpoint.h"
#include "omx_iface.h"
#include "omx_misc.h"
#include "omx_region.h"
#include "omx_common.h"
#include "omx_io.h"

static inline enum omx_nack_type
omx_shared_dst_endpoint_to_mcp_nack(struct omx_endpoint *dst_endpoint, uint32_t session_id)
{
	if (unlikely(IS_ERR(dst_endpoint)))
		return omx_endpoint_acquire_by_iface_index_error_to_nack_type(dst_endpoint);

	if (unlikely(session_id != dst_endpoint->session_id)) {
		omx_endpoint_release(dst_endpoint);
		return OMX_NACK_TYPE_BAD_SESSION;
	}

	return OMX_NACK_TYPE_NONE;
}

int
omx_shared_send_connect(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
			struct omx_cmd_send_connect_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_connect event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.shared = 1;
	event.length = hdr->length;
	event.seqnum = hdr->seqnum;

	/* copy the data */
	err = copy_from_user(&event.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send connect cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_CONNECT, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_CONNECT);
	omx_counter_inc(dst_endpoint->iface, RECV_CONNECT);

	return 0;

 out:
	return err;
}

int
omx_shared_send_tiny(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		     struct omx_cmd_send_tiny_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_msg event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.tiny.length = hdr->length;

	/* copy the data */
	err = copy_from_user(&event.specific.tiny.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send tiny cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_TINY, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_TINY);
	omx_counter_inc(dst_endpoint->iface, RECV_TINY);

	return 0;

 out:
	return err;
}

int
omx_shared_send_small(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		      struct omx_cmd_send_small *hdr)
{
	struct omx_evt_recv_msg event;
	unsigned long recvq_offset;
	int err;

	/* get the eventq slot */
	err = omx_prepare_notify_unexp_event_with_recvq(dst_endpoint, &recvq_offset);
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	/* copy the data */
	err = copy_from_user(dst_endpoint->recvq + recvq_offset,
			     (void *)(unsigned long) hdr->vaddr,
			     hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send small cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* fill and notify the event */
	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.small.length = hdr->length;
	event.specific.small.recvq_offset = recvq_offset;
	omx_commit_notify_unexp_event_with_recvq(dst_endpoint, OMX_EVT_RECV_SMALL, &event, sizeof(event));

	omx_counter_inc(src_endpoint->iface, SEND_SMALL);
	omx_counter_inc(dst_endpoint->iface, RECV_SMALL);

	return 0;

 out:
	return err;
}

int
omx_shared_send_medium(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		       struct omx_cmd_send_medium *hdr)
{
	struct omx_evt_recv_msg dst_event;
	struct omx_evt_send_medium_frag_done src_event;
	unsigned long recvq_offset;
	int err;

	/* get the dst eventq slot */
	err = omx_prepare_notify_unexp_event_with_recvq(dst_endpoint, &recvq_offset);
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	/* copy the data */
	memcpy(dst_endpoint->recvq + recvq_offset,
	       src_endpoint->sendq + (hdr->sendq_page_offset << PAGE_SHIFT),
	       hdr->frag_length);

	/* fill and notify the dst event */
	dst_event.peer_index = src_endpoint->iface->peer.index;
	dst_event.src_endpoint = src_endpoint->endpoint_index;
	dst_event.match_info = hdr->match_info;
	dst_event.seqnum = hdr->seqnum;
	dst_event.piggyack = hdr->piggyack;
	dst_event.specific.medium.msg_length = hdr->msg_length;
	dst_event.specific.medium.frag_length = hdr->frag_length;
	dst_event.specific.medium.frag_seqnum = hdr->frag_seqnum;
	dst_event.specific.medium.frag_pipeline = hdr->frag_pipeline;
	dst_event.specific.medium.recvq_offset = recvq_offset;
	omx_commit_notify_unexp_event_with_recvq(dst_endpoint, OMX_EVT_RECV_SMALL, &dst_event, sizeof(dst_event));

	/* fill and notify the src event */
	src_event.sendq_page_offset = hdr->sendq_page_offset;
	omx_notify_exp_event(src_endpoint, OMX_EVT_SEND_MEDIUM_FRAG_DONE, &src_event, sizeof(src_event));

	omx_counter_inc(src_endpoint->iface, SEND_MEDIUM_FRAG);
	omx_counter_inc(dst_endpoint->iface, RECV_MEDIUM_FRAG);

	return 0;

 out:
	return err;
}

int
omx_shared_send_rndv(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		     struct omx_cmd_send_rndv_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_msg event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.rndv.length = hdr->length;

	/* copy the data */
	err = copy_from_user(&event.specific.rndv.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send rndv cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_RNDV, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_RNDV);
	omx_counter_inc(dst_endpoint->iface, RECV_RNDV);

	return 0;

 out:
	return err;
}

int
omx_shared_pull(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		struct omx_cmd_pull *hdr)
{
	struct omx_evt_pull_done event;
	struct omx_user_region *src_region, *dst_region;
	enum omx_nack_type nack_type;
	int err;

	src_region = omx_user_region_acquire(src_endpoint, hdr->local_rdma_id);
	if (!src_region) {
		/* source region is invalid, return an error */
		err = -EINVAL;
		goto out;
	}

	nack_type = omx_shared_dst_endpoint_to_mcp_nack(dst_endpoint, hdr->session_id);
	if (unlikely(nack_type != OMX_NACK_TYPE_NONE)) {
		event.status = nack_type;
		goto notify;
	}

	dst_region = omx_user_region_acquire(dst_endpoint, hdr->remote_rdma_id);
	if (!dst_region) {
		/* dest region invalid, return a pull error */
		omx_user_region_release(src_region);
		event.status = OMX_EVT_PULL_DONE_BAD_RDMAWIN;
		goto notify;
	}

	omx_copy_between_user_regions(src_region, hdr->local_offset,
				      dst_region, hdr->remote_offset,
				      hdr->length);
	event.status = OMX_EVT_PULL_DONE_SUCCESS;
	err = 0;

	omx_user_region_release(dst_region);
	omx_user_region_release(src_region);

 notify:
	event.lib_cookie = hdr->lib_cookie;
	event.pulled_length = hdr->length; /* FIXME */
	event.local_rdma_id = hdr->local_rdma_id;

	/* notify the event */
	omx_notify_exp_event(src_endpoint, OMX_EVT_PULL_DONE, &event, sizeof(event));

	/* FIXME: counters */

 out:
	if (!IS_ERR(dst_endpoint))
		omx_endpoint_release(dst_endpoint);
	return err;
}

int
omx_shared_send_notify(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		       struct omx_cmd_send_notify *hdr)
{
	struct omx_evt_recv_msg event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.notify.length = hdr->total_length;
	event.specific.notify.puller_rdma_id = hdr->puller_rdma_id;
	event.specific.notify.puller_rdma_seqnum = hdr->puller_rdma_seqnum;

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_NOTIFY, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_NOTIFY);
	omx_counter_inc(dst_endpoint->iface, RECV_NOTIFY);

	return 0;

 out:
	return err;
}

int
omx_shared_send_truc(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		     struct omx_cmd_send_truc_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_truc event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.length = hdr->length;

	/* copy the data */
	err = copy_from_user(&event.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send truc cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_TRUC, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_TRUC);
	omx_counter_inc(dst_endpoint->iface, RECV_TRUC);

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
