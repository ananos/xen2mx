/*
 * Open-MX
 * Copyright � inria 2007-2010
 * Copyright � CNRS 2009
 * Copyright � Anastassios Nanos 2012
 * (see AUTHORS file)
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
#include <linux/skbuff.h>

//#define TIMERS_ENABLED
#include "omx_xen_timers.h"

#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_peer.h"
#include "omx_endpoint.h"
#include "omx_dma.h"


//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen_lib.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_reg.h"

timers_t t_recv, t_rndv, t_notify, t_small, t_tiny, t_medium, t_connect, t_truc;
/***************************
 * Event reporting routines
 */

static int
omx_recv_connect(struct omx_iface * iface,
		 struct omx_hdr * mh,
		 struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint64_t src_addr = omx_board_addr_from_ethhdr_src(eh);
	struct omx_peer *peer;
	uint32_t peer_index;
	struct omx_pkt_connect *connect_n = &mh->body.connect;
	uint8_t connect_data_length = OMX_NTOH_8(connect_n->length);
	uint8_t dst_endpoint = OMX_NTOH_8(connect_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(connect_n->src_endpoint);
	uint16_t reverse_peer_index = OMX_NTOH_16(connect_n->src_dst_peer_index);
	uint16_t lib_seqnum = OMX_NTOH_16(connect_n->lib_seqnum);
	uint8_t is_reply = OMX_NTOH_8(connect_n->generic.is_reply);
	int err = 0;

	dprintk_in();
	TIMER_START(&t_connect);
	/* check the connect data length */
	BUILD_BUG_ON(OMX_PKT_CONNECT_REQUEST_DATA_LENGTH != OMX_PKT_CONNECT_REPLY_DATA_LENGTH);
	if (connect_data_length < OMX_PKT_CONNECT_REQUEST_DATA_LENGTH) {
		omx_counter_inc(iface, DROP_BAD_DATALEN);
		omx_drop_dprintk(eh, "CONNECT packet too short (data length %d)",
				 (unsigned) connect_data_length);
		err = -EINVAL;
		goto out;
	}

	/* RCU section while manipulating peers */
	rcu_read_lock();

	/* the connect doesn't know its peer index yet, we need to lookup the peer */
	peer = omx_peer_lookup_by_addr_locked(src_addr);
	if (!peer) {
		rcu_read_unlock();
		omx_counter_inc(iface, DROP_BAD_PEER_ADDR);
		omx_drop_dprintk(eh, "CONNECT packet from unknown peer\n");
		goto out;
	}

	/* store our peer_index in the remote table */
	omx_peer_set_reverse_index(peer, iface, reverse_peer_index);

	peer_index = peer->index;

	/* end of RCU section while manipulating peers */
	rcu_read_unlock();

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "CONNECT packet for unknown endpoint %d",
				 dst_endpoint);
		/* it would be more clever to use the connect_seqnum (so that the receiver
		 * knows which connect request is being nacked), but the MX MCP does not know it.
		 * so just pass lib_seqnum to match the wire spec
		 */
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	if (endpoint->xen) {
		omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
		struct omx_xenif_response *ring_resp;
		dprintk_deb("XEN ENDPOINT! fw to the relevant domU via xenif@%#lx\n", (unsigned long) omx_xenif);

		ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		if (!is_reply) {
			struct omx_evt_recv_connect_request request_event;

			ring_resp->func = OMX_CMD_RECV_CONNECT_REQUEST;
			ring_resp->data.recv_connect_reply.board_index = endpoint->board_index;
			ring_resp->data.recv_connect_reply.eid = endpoint->endpoint_index;
			request_event.id = 0;
			request_event.type = OMX_EVT_RECV_CONNECT_REQUEST;
			request_event.peer_index = peer_index;
			request_event.src_endpoint = src_endpoint;
			request_event.shared = 0;
			request_event.seqnum = lib_seqnum;
			request_event.src_session_id = OMX_NTOH_32(connect_n->request.src_session_id);
			request_event.app_key = OMX_NTOH_32(connect_n->request.app_key);
			request_event.target_recv_seqnum_start = OMX_NTOH_16(connect_n->request.target_recv_seqnum_start);
			request_event.connect_seqnum = OMX_NTOH_8(connect_n->request.connect_seqnum);

			memcpy(&ring_resp->data.recv_connect_request.request, &request_event, sizeof(request_event));
			dump_xen_recv_connect_request(&ring_resp->data.recv_connect_request);
			omx_poke_domU(omx_xenif, ring_resp);

		} else {
			struct omx_evt_recv_connect_reply reply_event;

			ring_resp->func = OMX_CMD_RECV_CONNECT_REPLY;
			ring_resp->data.recv_connect_reply.board_index = endpoint->board_index;
			ring_resp->data.recv_connect_reply.eid = endpoint->endpoint_index;
			reply_event.id = 0;
			reply_event.type = OMX_EVT_RECV_CONNECT_REPLY;
			reply_event.peer_index = peer_index;
			reply_event.src_endpoint = src_endpoint;
			reply_event.shared = 0;
			reply_event.seqnum = lib_seqnum;
			reply_event.src_session_id = OMX_NTOH_32(connect_n->reply.src_session_id);
			reply_event.target_session_id = OMX_NTOH_32(connect_n->reply.target_session_id);
			reply_event.target_recv_seqnum_start = OMX_NTOH_16(connect_n->reply.target_recv_seqnum_start);
			reply_event.connect_seqnum = OMX_NTOH_8(connect_n->reply.connect_seqnum);
			reply_event.connect_status_code = OMX_NTOH_8(connect_n->reply.connect_status_code);
			BUILD_BUG_ON(OMX_CONNECT_STATUS_SUCCESS != OMX_PKT_CONNECT_STATUS_SUCCESS);
			BUILD_BUG_ON(OMX_CONNECT_STATUS_BAD_KEY != OMX_PKT_CONNECT_STATUS_BAD_KEY);

			memcpy(&ring_resp->data.recv_connect_reply.reply, &reply_event, sizeof(reply_event));
			dump_xen_recv_connect_reply(&ring_resp->data.recv_connect_reply);
			omx_poke_domU(omx_xenif, ring_resp);
		}


		goto xen_out;
	}

	/* fill event */
	if (!is_reply) {
		struct omx_evt_recv_connect_request request_event;

		request_event.id = 0;
		request_event.type = OMX_EVT_RECV_CONNECT_REQUEST;
		request_event.peer_index = peer_index;
		request_event.src_endpoint = src_endpoint;
		request_event.shared = 0;
		request_event.seqnum = lib_seqnum;
		request_event.src_session_id = OMX_NTOH_32(connect_n->request.src_session_id);
		request_event.app_key = OMX_NTOH_32(connect_n->request.app_key);
		request_event.target_recv_seqnum_start = OMX_NTOH_16(connect_n->request.target_recv_seqnum_start);
		request_event.connect_seqnum = OMX_NTOH_8(connect_n->request.connect_seqnum);

		/* notify the event */
		err = omx_notify_unexp_event(endpoint, &request_event, sizeof(request_event));

	} else {
		struct omx_evt_recv_connect_reply reply_event;

		reply_event.id = 0;
		reply_event.type = OMX_EVT_RECV_CONNECT_REPLY;
		reply_event.peer_index = peer_index;
		reply_event.src_endpoint = src_endpoint;
		reply_event.shared = 0;
		reply_event.seqnum = lib_seqnum;
		reply_event.src_session_id = OMX_NTOH_32(connect_n->reply.src_session_id);
		reply_event.target_session_id = OMX_NTOH_32(connect_n->reply.target_session_id);
		reply_event.target_recv_seqnum_start = OMX_NTOH_16(connect_n->reply.target_recv_seqnum_start);
		reply_event.connect_seqnum = OMX_NTOH_8(connect_n->reply.connect_seqnum);
		reply_event.connect_status_code = OMX_NTOH_8(connect_n->reply.connect_status_code);
		BUILD_BUG_ON(OMX_CONNECT_STATUS_SUCCESS != OMX_PKT_CONNECT_STATUS_SUCCESS);
		BUILD_BUG_ON(OMX_CONNECT_STATUS_BAD_KEY != OMX_PKT_CONNECT_STATUS_BAD_KEY);

		/* notify the event */
		err = omx_notify_unexp_event(endpoint, &reply_event, sizeof(reply_event));
	}

	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_drop_dprintk(eh, "CONNECT packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

xen_out:
	if (!is_reply)
		omx_counter_inc(iface, RECV_CONNECT_REQUEST);
	else
		omx_counter_inc(iface, RECV_CONNECT_REPLY);

	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	err = 0;
	goto r_out;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
 r_out:
	TIMER_STOP(&t_connect);
	dprintk_out();
	return err;
}

static int
omx_recv_tiny(struct omx_iface * iface,
	      struct omx_hdr * mh,
	      struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_msg *tiny_n = &mh->body.tiny;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	uint16_t length = OMX_NTOH_16(tiny_n->length);
	uint8_t dst_endpoint = OMX_NTOH_8(tiny_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(tiny_n->src_endpoint);
	uint32_t session_id = OMX_NTOH_32(tiny_n->session);
	uint16_t lib_seqnum = OMX_NTOH_16(tiny_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_NTOH_16(tiny_n->lib_piggyack);

	struct omx_evt_recv_msg event;
	int err = 0;

	TIMER_START(&t_tiny);
	/* check packet length */
	if (unlikely(length > OMX_TINY_MSG_LENGTH_MAX)) {
		omx_counter_inc(iface, DROP_BAD_DATALEN);
		omx_drop_dprintk(eh, "TINY packet too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, DROP_BAD_SKBLEN);
		omx_drop_dprintk(eh, "TINY packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "TINY packet with wrong peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "TINY packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(eh, "TINY packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(eh, "TINY length %ld", (unsigned long) length);

	if (endpoint->xen) {
		struct omx_evt_recv_msg *event;
		omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
		struct omx_xenif_response *ring_resp;
		dprintk_deb("XEN ENDPOINT! fw to the relevant domU via xenif@%#lx\n", (unsigned long) omx_xenif);

		ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		ring_resp->func = OMX_CMD_RECV_TINY;
		ring_resp->data.recv_msg.board_index = endpoint->board_index;
		ring_resp->data.recv_msg.eid = endpoint->endpoint_index;
		event = &ring_resp->data.recv_msg.msg;

		/* fill event */
		event->id = 0;
		event->type = OMX_EVT_RECV_TINY;
		event->peer_index = peer_index;
		event->src_endpoint = src_endpoint;
		event->match_info = OMX_NTOH_MATCH_INFO(tiny_n);
		event->seqnum = lib_seqnum;
		event->piggyack = lib_piggyack;
		event->specific.tiny.length = length;
		event->specific.tiny.checksum = OMX_NTOH_16(tiny_n->checksum);

		/* FIXME: is this correct ? we copy directly into the ring structure.
		 * What about concurrency ?????, how do we make sure that the ring won't overflow ?
		 */

		//memcpy(&ring_resp->data.recv_msg.msg, &event, sizeof(event));
		//memcpy(&ring_resp->data.recv_msg.msg.specific.tiny, &event.specific.tiny, sizeof(event.specific.tiny));
		//ring_resp->data.recv_msg.msg.specific.tiny.length = event.specific.tiny.length;
		//ring_resp->data.recv_msg.msg.specific.tiny.checksum = event.specific.tiny.checksum;
#if 0
		err = skb_copy_bits(skb, hdr_len, event.specific.tiny.data, length);
		BUG_ON(err < 0);
#endif
		err = skb_copy_bits(skb, hdr_len, ring_resp->data.recv_msg.msg.specific.tiny.data, length);
		BUG_ON(err < 0);
		//memcpy(ring_resp->data.recv_msg.msg.specific.tiny.data, event.specific.tiny.data, length);

		//dump_xen_recv_tiny(&ring_resp->data.recv_msg);
		TIMER_START(&endpoint->fe_endpoint->otherway);
		omx_poke_domU(omx_xenif, ring_resp);
		goto xen_out;
	}
	/* fill event */
	event.id = 0;
	event.type = OMX_EVT_RECV_TINY;
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_NTOH_MATCH_INFO(tiny_n);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.tiny.length = length;
	event.specific.tiny.checksum = OMX_NTOH_16(tiny_n->checksum);

#ifndef OMX_NORECVCOPY
	/* copy data in event data */
	err = skb_copy_bits(skb, hdr_len, event.specific.tiny.data, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);
#endif

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_drop_dprintk(eh, "TINY packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

xen_out:
	omx_counter_inc(iface, RECV_TINY);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	TIMER_STOP(&t_tiny);
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	TIMER_STOP(&t_tiny);
	return err;
}

static int
omx_recv_small(struct omx_iface * iface,
	       struct omx_hdr * mh,
	       struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_msg *small_n = &mh->body.small;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	uint16_t length =  OMX_NTOH_16(small_n->length);
	uint8_t dst_endpoint = OMX_NTOH_8(small_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(small_n->src_endpoint);
	uint32_t session_id = OMX_NTOH_32(small_n->session);
	uint16_t lib_seqnum = OMX_NTOH_16(small_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_NTOH_16(small_n->lib_piggyack);
	struct omx_evt_recv_msg event;
	unsigned long recvq_offset;
	int err;

	dprintk_in();
	TIMER_START(&t_small);
	BUILD_BUG_ON(OMX_SMALL_MSG_LENGTH_MAX > OMX_RECVQ_ENTRY_SIZE);

	/* check packet length */
	if (unlikely(length > OMX_SMALL_MSG_LENGTH_MAX)) {
		omx_counter_inc(iface, DROP_BAD_DATALEN);
		omx_drop_dprintk(eh, "SMALL packet too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, DROP_BAD_SKBLEN);
		omx_drop_dprintk(eh, "SMALL packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "SMALL packet with wrong peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "SMALL packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(eh, "SMALL packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

        if (endpoint->xen) {
                omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
                struct omx_xenif_response *ring_resp;
		uint16_t offset;
                dprintk_deb("XEN ENDPOINT! have to get a recvq offset and poke the frontend via xenif@%#lx\n", (unsigned long) omx_xenif);

		/* FIXME: no locks, no protection!
		 * We can call this function directly, because we map the frontend's *
	         * indices into the backend */

		/* get the eventq slot */
		err = omx_prepare_notify_unexp_event_with_recvq(endpoint, &recvq_offset);
		if (unlikely(err < 0)) {
			/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
			printk_err("xen unexp_event_queue_full!!!\n");
			omx_drop_dprintk(eh, "SMALL packet because of unexpected event queue full");
			goto out_with_endpoint;
		}

		ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		ring_resp->func = OMX_CMD_RECV_SMALL;
		ring_resp->data.recv_msg.board_index = endpoint->board_index;
		ring_resp->data.recv_msg.eid = endpoint->endpoint_index;

		ring_resp->data.recv_msg.xen_nextfree_unexp_eventq_index = endpoint->nextfree_unexp_eventq_index;
		ring_resp->data.recv_msg.xen_nextreserved_unexp_eventq_index = endpoint->nextreserved_unexp_eventq_index;
		ring_resp->data.recv_msg.xen_nextreleased_unexp_eventq_index = endpoint->nextreleased_unexp_eventq_index;
		ring_resp->data.recv_msg.xen_next_recvq_index = endpoint->next_recvq_index;
		ring_resp->data.recv_msg.recvq_offset = recvq_offset;

		/* fill event */
		event.id = 0;
		event.type = OMX_EVT_RECV_SMALL;
		event.peer_index = peer_index;
		event.src_endpoint = src_endpoint;
		event.match_info = OMX_NTOH_MATCH_INFO(small_n);
		event.seqnum = lib_seqnum;
		event.piggyack = lib_piggyack;
		event.specific.small.length = length;

		event.specific.small.checksum = OMX_NTOH_16(small_n->checksum);

		omx_recv_dprintk(eh, "SMALL length %ld", (unsigned long) length);

		memcpy(&ring_resp->data.recv_msg.msg, &event, sizeof(event));

		event.specific.small.recvq_offset = recvq_offset;
		dprintk_deb("%s: recvq_offset = %#x\n", __func__, recvq_offset);

		memcpy(&ring_resp->data.recv_msg.msg.specific.small, &event.specific.small, sizeof(event.specific.small));
#if 1
		offset = recvq_offset &~PAGE_MASK;
		if (offset)
			printk_inf("offset = %#x\n", offset);
		/* copy data in recvq slot */
		err = skb_copy_bits(skb, hdr_len, pfn_to_kaddr(page_to_pfn((endpoint->xen_recvq_pages[recvq_offset>>PAGE_SHIFT]))) + offset, length);
		/* cannot fail since pages are allocated by us */
		BUG_ON(err < 0);
#endif

                omx_poke_domU(omx_xenif, ring_resp);
                goto xen_out;
        }
	else
	{
		/* get the eventq slot */
		err = omx_prepare_notify_unexp_event_with_recvq(endpoint, &recvq_offset);
		if (unlikely(err < 0)) {
			/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
			omx_drop_dprintk(eh, "SMALL packet because of unexpected event queue full");
			goto out_with_endpoint;
		}
	}

	/* fill event */
	event.id = 0;
	event.type = OMX_EVT_RECV_SMALL;
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_NTOH_MATCH_INFO(small_n);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.small.length = length;
	event.specific.small.recvq_offset = recvq_offset;
	event.specific.small.checksum = OMX_NTOH_16(small_n->checksum);

	omx_recv_dprintk(eh, "SMALL length %ld", (unsigned long) length);

#ifndef OMX_NORECVCOPY
	/* copy data in recvq slot */
	err = skb_copy_bits(skb, hdr_len, endpoint->recvq + recvq_offset, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);
#endif

	/* notify the event */
	omx_commit_notify_unexp_event_with_recvq(endpoint, &event, sizeof(event));

xen_out:
	omx_counter_inc(iface, RECV_SMALL);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	TIMER_STOP(&t_small);
	dprintk_out();
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	TIMER_STOP(&t_small);
	dprintk_out();
	return err;
}

static int
omx_recv_medium_frag(struct omx_iface * iface,
		     struct omx_hdr * mh,
		     struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_medium_frag *medium_n = &mh->body.medium;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_medium_frag);
	uint16_t frag_length = OMX_NTOH_16(medium_n->frag_length);
	uint8_t dst_endpoint = OMX_NTOH_8(medium_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(medium_n->src_endpoint);
	uint32_t session_id = OMX_NTOH_32(medium_n->session);
	uint16_t lib_seqnum = OMX_NTOH_16(medium_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_NTOH_16(medium_n->lib_piggyack);
	uint32_t actual_length = 0;
	uint32_t pgidx = 0;
	uint32_t skb_offset = 0;

	struct omx_evt_recv_msg event;
	unsigned long recvq_offset;
	int remaining_copy = frag_length;
#ifdef OMX_HAVE_DMA_ENGINE
	struct dma_chan *dma_chan = NULL;
	dma_cookie_t dma_cookie = 0;
#endif
	int err;
	void *staging;
	dprintk_in();

	TIMER_START(&t_medium);
	BUILD_BUG_ON(OMX_MEDIUM_FRAG_LENGTH_MAX > OMX_RECVQ_ENTRY_SIZE);

	/* check packet length */
	if (unlikely(frag_length > OMX_RECVQ_ENTRY_SIZE)) {
		omx_counter_inc(iface, DROP_BAD_DATALEN);
		omx_drop_dprintk(eh, "MEDIUM fragment packet too long (length %d)",
				 (unsigned) frag_length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(frag_length > skb->len - hdr_len)) {
		omx_counter_inc(iface, DROP_BAD_SKBLEN);
		omx_drop_dprintk(eh, "MEDIUM fragment with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) frag_length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "MEDIUM packet with wrong peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "MEDIUM packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(eh, "MEDIUM packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

        if (endpoint->xen) {
                omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
                struct omx_xenif_response *ring_resp;
                dprintk_deb("XEN ENDPOINT! have to get a recvq offset and poke the frontend via xenif@%#lx\n", (unsigned long) omx_xenif);

		/* FIXME: no locks, no protection!
		 * We can call this function directly, because we map the frontend's *
	         * indices into the backend */

		/* get the eventq slot */
		err = omx_prepare_notify_unexp_event_with_recvq(endpoint, &recvq_offset);
		if (unlikely(err < 0)) {
			/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
			printk_err("xen unexp_event_queue_full!!!\n");
			omx_drop_dprintk(eh, "SMALL packet because of unexpected event queue full");
			goto out_with_endpoint;
		}

		ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		ring_resp->func = OMX_CMD_RECV_MEDIUM_FRAG;
		ring_resp->data.recv_msg.board_index = endpoint->board_index;
		ring_resp->data.recv_msg.eid = endpoint->endpoint_index;
		ring_resp->data.recv_msg.xen_nextfree_unexp_eventq_index = endpoint->nextfree_unexp_eventq_index;
		ring_resp->data.recv_msg.xen_nextreserved_unexp_eventq_index = endpoint->nextreserved_unexp_eventq_index;
		ring_resp->data.recv_msg.xen_nextreleased_unexp_eventq_index = endpoint->nextreleased_unexp_eventq_index;
		ring_resp->data.recv_msg.xen_next_recvq_index = endpoint->next_recvq_index;
		ring_resp->data.recv_msg.recvq_offset = recvq_offset;

		/* fill event */
		event.id = 0;
		event.type = OMX_EVT_RECV_MEDIUM_FRAG;
		event.peer_index = peer_index;
		event.src_endpoint = src_endpoint;
		event.match_info = OMX_NTOH_MATCH_INFO(medium_n);
		event.seqnum = lib_seqnum;
		event.piggyack = lib_piggyack;
#ifdef OMX_MX_WIRE_COMPAT
		event.specific.medium_frag.msg_length = OMX_NTOH_16(medium_n->length);
		event.specific.medium_frag.frag_pipeline = OMX_NTOH_8(medium_n->frag_pipeline);
#else
		event.specific.medium_frag.msg_length = OMX_NTOH_32(medium_n->length);
#endif
		event.specific.medium_frag.frag_length = frag_length;
		event.specific.medium_frag.frag_seqnum = OMX_NTOH_8(medium_n->frag_seqnum);
		event.specific.medium_frag.checksum = OMX_NTOH_16(medium_n->checksum);

		event.specific.medium_frag.recvq_offset = recvq_offset;

		//dprintk_inf("%s: recvq_offset = %#x\n", __func__, recvq_offset);
		omx_recv_dprintk(eh, "MEDIUM_FRAG length %ld", (unsigned long) frag_length);
		memcpy(&ring_resp->data.recv_msg.msg, &event, sizeof(event));
		memcpy(&ring_resp->data.recv_msg.msg.specific.medium_frag, &event.specific.medium_frag, sizeof(event.specific.medium_frag));

#if 1
		/* copy what's remaining */
		actual_length = frag_length;
		pgidx = recvq_offset;
		skb_offset = hdr_len;

		/* FIXME: stage data, until we find the source of corruption */
		staging = kmalloc(remaining_copy, GFP_ATOMIC);
		err = skb_copy_bits(skb, hdr_len, staging, remaining_copy);
		/* cannot fail since pages are allocated by us */
		BUG_ON(err < 0);
		while (remaining_copy) {
			int offset = recvq_offset &~PAGE_MASK;
			struct page *page = endpoint->xen_recvq_pages[pgidx>>PAGE_SHIFT];
			void *data_vaddr = pfn_to_kaddr(page_to_pfn((page)));
			actual_length = remaining_copy > PAGE_SIZE ? PAGE_SIZE : remaining_copy;
			//dprintk_inf("remaining_copy = %#x, actual_length %#x, offset = %#x\n", remaining_copy, actual_length, offset);
			//dprintk_inf("page = %#x, vaddr=%#x, recvq_offset = %#x, pgidx=%#x\n", page, data_vaddr, recvq_offset, pgidx>>PAGE_SHIFT);
			/* cannot fail since pages are allocated by us */
			BUG_ON(err < 0);
			memcpy(data_vaddr + offset, staging + pgidx - recvq_offset, actual_length);
			remaining_copy -= actual_length;
			pgidx += actual_length;
		}
		kfree(staging);
#endif
                omx_poke_domU(omx_xenif, ring_resp);
		goto xen_out;

	}
	else
	{
		/* get the eventq slot */
		err = omx_prepare_notify_unexp_event_with_recvq(endpoint, &recvq_offset);
		if (unlikely(err < 0)) {
			/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
			omx_drop_dprintk(eh, "MEDIUM packet because of unexpected event queue full");
			goto out_with_endpoint;
		}
	}

#if (defined OMX_HAVE_DMA_ENGINE) && !(defined OMX_NORECVCOPY)
	/* try to submit the dma copy */
	if (omx_dmaengine && frag_length >= omx_dma_sync_min) {
		dma_chan = omx_dma_chan_get();
		if (dma_chan) {
			/* if multiple pages per ring entry:
			 *   copy several pages, with the ring entries always page aligned, and no wrap around the ring
			 * if one or less pages per ring entry:
			 *   copy one page or less, always within the same page, but not necessarily starting aligned on a page
			 */
			struct page ** pages = &endpoint->recvq_pages[recvq_offset >> PAGE_SHIFT];
			remaining_copy = omx_dma_skb_copy_datagram_to_pages(dma_chan, &dma_cookie,
									    skb, hdr_len,
									    pages, recvq_offset & (~PAGE_MASK) /* 0 if multiple pages */,
									    frag_length);
			dma_async_memcpy_issue_pending(dma_chan);
			if (remaining_copy) {
				printk(KERN_INFO "Open-MX: DMA copy of medium frag partially submitted, %d/%d remaining\n",
				       remaining_copy, (unsigned) frag_length);
				omx_counter_inc(iface, DMARECV_PARTIAL_MEDIUM_FRAG);
			} else {
				omx_counter_inc(iface, DMARECV_MEDIUM_FRAG);
			}
		}
	}
#endif

	/* fill event */
	event.id = 0;
	event.type = OMX_EVT_RECV_MEDIUM_FRAG;
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_NTOH_MATCH_INFO(medium_n);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
#ifdef OMX_MX_WIRE_COMPAT
	event.specific.medium_frag.msg_length = OMX_NTOH_16(medium_n->length);
	event.specific.medium_frag.frag_pipeline = OMX_NTOH_8(medium_n->frag_pipeline);
#else
	event.specific.medium_frag.msg_length = OMX_NTOH_32(medium_n->length);
#endif
	event.specific.medium_frag.frag_length = frag_length;
	event.specific.medium_frag.frag_seqnum = OMX_NTOH_8(medium_n->frag_seqnum);
	event.specific.medium_frag.checksum = OMX_NTOH_16(medium_n->checksum);
	event.specific.medium_frag.recvq_offset = recvq_offset;

	omx_recv_dprintk(eh, "MEDIUM_FRAG length %ld", (unsigned long) frag_length);

#ifndef OMX_NORECVCOPY
	/* copy what's remaining */
	if (remaining_copy) {
		int offset = frag_length - remaining_copy;
		err = skb_copy_bits(skb, hdr_len + offset, endpoint->recvq + recvq_offset + offset, remaining_copy);
		/* cannot fail since pages are allocated by us */
		BUG_ON(err < 0);
	}

	/* end the offloaded copy */
#ifdef OMX_HAVE_DMA_ENGINE
	if (dma_chan) {
		if (dma_cookie > 0)
			while (dma_async_memcpy_complete(dma_chan, dma_cookie, NULL, NULL) == DMA_IN_PROGRESS);
		omx_dma_chan_put(dma_chan);
	}
#endif
#endif /* OMX_NORECVCOPY */

	/* notify the event */
	omx_commit_notify_unexp_event_with_recvq(endpoint, &event, sizeof(event));

xen_out:
	omx_counter_inc(iface, RECV_MEDIUM_FRAG);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	TIMER_STOP(&t_medium);
	dprintk_out();
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	TIMER_STOP(&t_medium);
	dprintk_out();
	return err;
}

static int
omx_recv_rndv(struct omx_iface * iface,
	      struct omx_hdr * mh,
	      struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_rndv *rndv_n = &mh->body.rndv;
	uint16_t rndv_data_length = OMX_NTOH_16(rndv_n->msg.length);
	uint8_t dst_endpoint = OMX_NTOH_8(rndv_n->msg.dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(rndv_n->msg.src_endpoint);
	uint32_t session_id = OMX_NTOH_32(rndv_n->msg.session);
	uint16_t lib_seqnum = OMX_NTOH_16(rndv_n->msg.lib_seqnum);
	uint16_t lib_piggyack = OMX_NTOH_16(rndv_n->msg.lib_piggyack);
	struct omx_evt_recv_msg event;
	int err = 0;

	dprintk_in();
	TIMER_START(&t_rndv);
	/* check the rdnv data length */
	if (rndv_data_length < OMX_PKT_RNDV_DATA_LENGTH) {
		omx_counter_inc(iface, DROP_BAD_DATALEN);
		omx_drop_dprintk(eh, "RNDV packet too short (data length %d)",
				 (unsigned) rndv_data_length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "RNDV packet with wrong peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "RNDV packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(eh, "RNDV packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(eh, "RNDV");

	if (endpoint->xen) {
		struct omx_evt_recv_msg event;
		omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
		struct omx_xenif_response *ring_resp;
		dprintk_deb("XEN ENDPOINT! fw to the relevant domU via xenif@%#lx\n", (unsigned long) omx_xenif);

		ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		ring_resp->func = OMX_CMD_RECV_RNDV;
		ring_resp->data.recv_msg.board_index = endpoint->board_index;
		ring_resp->data.recv_msg.eid = endpoint->endpoint_index;

		event.id = 0;
		event.type = OMX_EVT_RECV_RNDV;
		event.peer_index = peer_index;
		event.src_endpoint = src_endpoint;
		event.match_info = OMX_NTOH_MATCH_INFO(&rndv_n->msg);
		event.seqnum = lib_seqnum;
		event.piggyack = lib_piggyack;
		event.specific.rndv.msg_length = OMX_NTOH_32(rndv_n->msg_length);
		event.specific.rndv.pulled_rdma_id = OMX_NTOH_8(rndv_n->pulled_rdma_id);
		event.specific.rndv.pulled_rdma_seqnum = OMX_NTOH_8(rndv_n->pulled_rdma_seqnum);
		event.specific.rndv.pulled_rdma_offset = OMX_NTOH_16(rndv_n->pulled_rdma_offset);
		event.specific.rndv.checksum = OMX_NTOH_16(rndv_n->msg.checksum);

		memcpy(&ring_resp->data.recv_msg.msg, &event, sizeof(event));
		memcpy(&ring_resp->data.recv_msg.msg.specific.rndv, &event.specific.rndv, sizeof(event.specific.rndv));

		dump_xen_recv_msg(&ring_resp->data.recv_msg);
		omx_poke_domU(omx_xenif, ring_resp);
		goto xen_out;
	}
	/* fill event */
	event.id = 0;
	event.type = OMX_EVT_RECV_RNDV;
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_NTOH_MATCH_INFO(&rndv_n->msg);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.rndv.msg_length = OMX_NTOH_32(rndv_n->msg_length);
	event.specific.rndv.pulled_rdma_id = OMX_NTOH_8(rndv_n->pulled_rdma_id);
	event.specific.rndv.pulled_rdma_seqnum = OMX_NTOH_8(rndv_n->pulled_rdma_seqnum);
	event.specific.rndv.pulled_rdma_offset = OMX_NTOH_16(rndv_n->pulled_rdma_offset);
	event.specific.rndv.checksum = OMX_NTOH_16(rndv_n->msg.checksum);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_drop_dprintk(eh, "RNDV packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

xen_out:
	omx_counter_inc(iface, RECV_RNDV);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	TIMER_STOP(&t_rndv);
	dprintk_out();
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	TIMER_STOP(&t_rndv);
	dprintk_out();
	return err;
}

static int
omx_recv_notify(struct omx_iface * iface,
		struct omx_hdr * mh,
		struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_notify *notify_n = &mh->body.notify;
	uint8_t dst_endpoint = OMX_NTOH_8(notify_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(notify_n->src_endpoint);
	uint32_t session_id = OMX_NTOH_32(notify_n->session);
	uint16_t lib_seqnum = OMX_NTOH_16(notify_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_NTOH_16(notify_n->lib_piggyack);
	struct omx_evt_recv_msg event;
	int err = 0;

	dprintk_in();
	TIMER_START(&t_notify);
	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "NOTIFY packet with wrong peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "NOTIFY packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(eh, "NOTIFY packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(eh, "NOTIFY");

	if (endpoint->xen) {
		struct omx_evt_recv_msg event;
		omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
		struct omx_xenif_response *ring_resp;
		dprintk_deb("XEN ENDPOINT! fw to the relevant domU via xenif@%#lx\n", (unsigned long) omx_xenif);

		ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
		ring_resp->func = OMX_CMD_RECV_NOTIFY;
		ring_resp->data.recv_msg.board_index = endpoint->board_index;
		ring_resp->data.recv_msg.eid = endpoint->endpoint_index;

		/* fill event */
		event.id = 0;
		event.type = OMX_EVT_RECV_NOTIFY;
		event.peer_index = peer_index;
		event.src_endpoint = src_endpoint;
		event.seqnum = lib_seqnum;
		event.piggyack = lib_piggyack;
		event.specific.notify.length = OMX_NTOH_32(notify_n->total_length);
		event.specific.notify.pulled_rdma_id = OMX_NTOH_8(notify_n->pulled_rdma_id);
		event.specific.notify.pulled_rdma_seqnum = OMX_NTOH_8(notify_n->pulled_rdma_seqnum);

		memcpy(&ring_resp->data.recv_msg.msg, &event, sizeof(event));
		memcpy(&ring_resp->data.recv_msg.msg.specific.notify, &event.specific.notify, sizeof(event.specific.notify));

		dump_xen_recv_notify(&ring_resp->data.recv_msg);
		omx_poke_domU(omx_xenif, ring_resp);
		goto xen_out;
	}

	/* fill event */
	event.id = 0;
	event.type = OMX_EVT_RECV_NOTIFY;
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.notify.length = OMX_NTOH_32(notify_n->total_length);
	event.specific.notify.pulled_rdma_id = OMX_NTOH_8(notify_n->pulled_rdma_id);
	event.specific.notify.pulled_rdma_seqnum = OMX_NTOH_8(notify_n->pulled_rdma_seqnum);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_drop_dprintk(eh, "NOTIFY packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

xen_out:
	omx_counter_inc(iface, RECV_NOTIFY);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	TIMER_STOP(&t_notify);
	dprintk_out();
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	TIMER_STOP(&t_notify);
	dprintk_out();
	return err;
}

static int
omx_recv_truc(struct omx_iface * iface,
	      struct omx_hdr * mh,
	      struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_truc *truc_n = &mh->body.truc;
	uint8_t data_length = OMX_NTOH_8(truc_n->length);
	uint8_t dst_endpoint = OMX_NTOH_8(truc_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(truc_n->src_endpoint);
	uint32_t session_id = OMX_NTOH_32(truc_n->session);
	uint8_t truc_type = OMX_NTOH_8(truc_n->type);
	int err = 0;

	dprintk_in();
	TIMER_START(&t_truc);
	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "TRUC packet with wrong peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "TRUC packet for unknown endpoint %d",
				 dst_endpoint);
		/* no nack for truc messages, just drop */
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, DROP_BAD_SESSION);
		omx_drop_dprintk(eh, "TRUC packet with bad session");
		/* no nack for truc messages, just drop */
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(eh, "TRUC");
	switch (truc_type) {
	case OMX_PKT_TRUC_DATA_TYPE_ACK: {
		struct omx_evt_recv_liback liback_event;

		if (unlikely(data_length < OMX_PKT_TRUC_LIBACK_DATA_LENGTH)) {
			omx_counter_inc(iface, DROP_BAD_DATALEN);
			omx_drop_dprintk(eh, "TRUC LIBACK packet too short (data length %d)",
					 (unsigned) data_length);
			err = -EINVAL;
			goto out_with_endpoint;
		}

		if (unlikely(session_id != OMX_NTOH_32(truc_n->liback.session_id))) {
			omx_counter_inc(iface, DROP_BAD_SESSION);
			omx_drop_dprintk(eh, "TRUC LIBACK packet with bad session");
			/* no nack for truc messages, just drop */
			err = -EINVAL;
			goto out_with_endpoint;
		}

		/* fill event */
		liback_event.id = 0;
		liback_event.type = OMX_EVT_RECV_LIBACK;
		liback_event.peer_index = peer_index;
		liback_event.src_endpoint = src_endpoint;
		liback_event.lib_seqnum = OMX_NTOH_16(truc_n->liback.lib_seqnum);
		liback_event.acknum = OMX_NTOH_32(truc_n->liback.acknum);
		liback_event.send_seq = OMX_NTOH_16(truc_n->liback.send_seq);
		liback_event.resent = OMX_NTOH_8(truc_n->liback.resent);

		if (endpoint->xen) {
			omx_xenif_t * omx_xenif = endpoint->be->omx_xenif;
			struct omx_xenif_response *ring_resp;
			dprintk_deb("XEN ENDPOINT! fw to the relevant domU via xenif@%#lx\n", (unsigned long) omx_xenif);

			ring_resp = RING_GET_RESPONSE(&(omx_xenif->recv_ring), omx_xenif->recv_ring.rsp_prod_pvt++);
			ring_resp->func = OMX_CMD_RECV_LIBACK;
			ring_resp->data.recv_msg.board_index = endpoint->board_index;
			ring_resp->data.recv_msg.eid = endpoint->endpoint_index;

			memcpy(&ring_resp->data.recv_liback.liback, &liback_event, sizeof(liback_event));
			dump_xen_recv_liback(&ring_resp->data.recv_liback);
			omx_poke_domU(omx_xenif, ring_resp);
			break;
		}
		else
		{
			/* notify the event */
			err = omx_notify_unexp_event(endpoint, &liback_event, sizeof(liback_event));
			break;
		}
	}
	default:
		omx_drop_dprintk(eh, "TRUC packet because of unknown truc type %d",
				 truc_type);
		goto out_with_endpoint;
	}

	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_drop_dprintk(eh, "TRUC packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

//xen_out:
	omx_counter_inc(iface, RECV_LIBACK);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	TIMER_STOP(&t_truc);
	dprintk_out();
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dprintk_out();
	TIMER_STOP(&t_truc);
	dev_kfree_skb(skb);
	return err;
}

static int
omx_recv_nack_lib(struct omx_iface * iface,
		  struct omx_hdr * mh,
		  struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_NTOH_16(mh->head.dst_src_peer_index);
	struct omx_pkt_nack_lib *nack_lib_n = &mh->body.nack_lib;
	uint8_t dst_endpoint = OMX_NTOH_8(nack_lib_n->dst_endpoint);
	uint8_t src_endpoint = OMX_NTOH_8(nack_lib_n->src_endpoint);
	enum omx_nack_type nack_type = OMX_NTOH_8(nack_lib_n->nack_type);
	uint16_t lib_seqnum = OMX_NTOH_16(nack_lib_n->lib_seqnum);
	struct omx_evt_recv_nack_lib event;
	int err = 0;

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index,
					omx_board_addr_from_ethhdr_src(eh));
	if (unlikely(err < 0)) {
		/* FIXME: impossible? in non MX-wire compatible only? */
		struct omx_peer *peer;
		uint64_t src_addr;

		if (peer_index != (uint16_t)-1) {
			omx_drop_dprintk(eh, "NACK LIB with bad peer index %d",
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
			omx_drop_dprintk(eh, "NACK LIB packet from unknown peer\n");
			goto out;
		}

		peer_index = peer->index;

		/* end of RCU section while manipulating peers */
		rcu_read_unlock();
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "NACK LIB packet for unknown endpoint %d",
				 dst_endpoint);
		/* FIXME: BUG? */
		err = PTR_ERR(endpoint);
		goto out;
	}

	omx_recv_dprintk(eh, "NACK LIB type %s",
			 omx_strnacktype(nack_type));

	/* fill event */
	event.id = 0;
	event.type = OMX_EVT_RECV_NACK_LIB;
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.seqnum = lib_seqnum;
	/* enforce that nack type and pull status have same values */
	BUILD_BUG_ON(OMX_EVT_NACK_LIB_BAD_ENDPT != OMX_NACK_TYPE_BAD_ENDPT);
	BUILD_BUG_ON(OMX_EVT_NACK_LIB_ENDPT_CLOSED != OMX_NACK_TYPE_ENDPT_CLOSED);
	BUILD_BUG_ON(OMX_EVT_NACK_LIB_BAD_SESSION != OMX_NACK_TYPE_BAD_SESSION);
	event.nack_type = nack_type;

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_drop_dprintk(eh, "NACK LIB packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_counter_inc(iface, RECV_NACK_LIB);
	omx_endpoint_release(endpoint);
	dev_kfree_skb(skb);
	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	dev_kfree_skb(skb);
	return err;
}

#if 0
static int
omx_recv_nosys(struct omx_iface * iface,
		struct omx_hdr * mh,
		struct sk_buff * skb)
{
	omx_counter_inc(iface, DROP_NOSYS_TYPE);
	omx_drop_dprintk(&mh->head.eth, "packet with unsupported type %d",
			 mh->body.generic.ptype);

	dev_kfree_skb(skb);
	return 0;
}
#endif

static int
omx_recv_invalid(struct omx_iface * iface,
		 struct omx_hdr * mh,
		 struct sk_buff * skb)
{
	omx_counter_inc(iface, DROP_INVALID_TYPE);
	omx_drop_dprintk(&mh->head.eth, "packet with invalid type %d",
			 mh->body.generic.ptype);

	dev_kfree_skb(skb);
	return 0;
}

static int
omx_recv_error(struct omx_iface * iface,
		struct omx_hdr * mh,
		struct sk_buff * skb)
{
	omx_counter_inc(iface, DROP_UNKNOWN_TYPE);
	omx_drop_dprintk(&mh->head.eth, "packet with unrecognized type %d",
			 mh->body.generic.ptype);

	dev_kfree_skb(skb);
	return 0;
}

/***********************
 * Packet type handlers
 */

static int (*omx_pkt_type_handler[OMX_PKT_TYPE_MAX+1])(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
static size_t omx_pkt_type_hdr_len[OMX_PKT_TYPE_MAX+1];

void
omx_pkt_types_init(void)
{
	int i;

	for(i=0; i<=OMX_PKT_TYPE_MAX; i++) {
		omx_pkt_type_handler[i] = omx_recv_error;
		omx_pkt_type_hdr_len[i] = sizeof(struct omx_pkt_head);
	}

	omx_pkt_type_handler[OMX_PKT_TYPE_RAW] = omx_recv_raw;
	omx_pkt_type_handler[OMX_PKT_TYPE_MFM_NIC_REPLY] = omx_recv_invalid;
	omx_pkt_type_handler[OMX_PKT_TYPE_HOST_QUERY] = omx_recv_host_query;
	omx_pkt_type_handler[OMX_PKT_TYPE_HOST_REPLY] = omx_recv_host_reply;
	omx_pkt_type_handler[OMX_PKT_TYPE_ETHER_UNICAST] = omx_recv_invalid;
	omx_pkt_type_handler[OMX_PKT_TYPE_ETHER_MULTICAST] = omx_recv_invalid;
	omx_pkt_type_handler[OMX_PKT_TYPE_ETHER_NATIVE] = omx_recv_invalid;
	omx_pkt_type_handler[OMX_PKT_TYPE_TRUC] = omx_recv_truc;
	omx_pkt_type_handler[OMX_PKT_TYPE_CONNECT] = omx_recv_connect;
	omx_pkt_type_handler[OMX_PKT_TYPE_TINY] = omx_recv_tiny;
	omx_pkt_type_handler[OMX_PKT_TYPE_SMALL] = omx_recv_small;
	omx_pkt_type_handler[OMX_PKT_TYPE_MEDIUM] = omx_recv_medium_frag;
	omx_pkt_type_handler[OMX_PKT_TYPE_RNDV] = omx_recv_rndv;
	omx_pkt_type_handler[OMX_PKT_TYPE_PULL] = omx_recv_pull_request;
	omx_pkt_type_handler[OMX_PKT_TYPE_PULL_REPLY] = omx_recv_pull_reply;
	omx_pkt_type_handler[OMX_PKT_TYPE_NOTIFY] = omx_recv_notify;
	omx_pkt_type_handler[OMX_PKT_TYPE_NACK_LIB] = omx_recv_nack_lib;
	omx_pkt_type_handler[OMX_PKT_TYPE_NACK_MCP] = omx_recv_nack_mcp;

	omx_pkt_type_hdr_len[OMX_PKT_TYPE_RAW] += 0; /* only user-space will dereference more than omx_pkt_head */
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_HOST_QUERY] += sizeof(struct omx_pkt_host_query);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_HOST_REPLY] += sizeof(struct omx_pkt_host_reply);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_TRUC] += sizeof(struct omx_pkt_truc);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_CONNECT] += sizeof(struct omx_pkt_connect);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_TINY] += sizeof(struct omx_pkt_msg);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_SMALL] += sizeof(struct omx_pkt_msg);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_MEDIUM] += sizeof(struct omx_pkt_medium_frag);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_RNDV] += sizeof(struct omx_pkt_msg);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_PULL] += sizeof(struct omx_pkt_pull_request);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_PULL_REPLY] += sizeof(struct omx_pkt_pull_reply);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_NOTIFY] += sizeof(struct omx_pkt_notify);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_NACK_LIB] += sizeof(struct omx_pkt_nack_lib);
	omx_pkt_type_hdr_len[OMX_PKT_TYPE_NACK_MCP] += sizeof(struct omx_pkt_nack_mcp);

	/* make sure the packet is always large enough to contain the required headers */
	BUILD_BUG_ON(sizeof(struct omx_hdr) > ETH_ZLEN);
}

/***********************
 * Main receive routine
 */

static int
omx_recv(struct sk_buff *skb, struct net_device *ifp, struct packet_type *pt,
	  struct net_device *orig_dev)
{
	struct omx_iface *iface;
	struct omx_hdr linear_header;
	struct omx_hdr *mh;
	omx_packet_type_t ptype;
	size_t hdr_len;
	int err = 0;

	dprintk_in();

	TIMER_START(&t_recv);
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(skb == NULL)) {
		err = 0;
		goto out;
	}

	/* len doesn't include header */
	skb_push(skb, ETH_HLEN);

	iface = omx_iface_find_by_ifp(ifp);
	if (unlikely(!iface)) {
		/* at least the ethhdr is linear in the skb */
		omx_drop_dprintk(&omx_skb_mac_header(skb)->head.eth, "packet on non-Open-MX interface %s",
				 ifp->name);
		goto out;
	}

	/* pointer to the data, assuming it is linear */
	mh = omx_skb_mac_header(skb);

	/* make sure we can always dereference omx_pkt_head and ptype in incoming skb */
	BUILD_BUG_ON(ETH_ZLEN < sizeof(struct omx_pkt_head));
	BUILD_BUG_ON(ETH_ZLEN < OMX_HDR_PTYPE_OFFSET + sizeof(omx_packet_type_t));
#ifdef OMX_DRIVER_DEBUG
	if (skb->len < ETH_ZLEN) {
		omx_counter_inc(iface, DROP_BAD_HEADER_DATALEN);
		omx_drop_dprintk(&mh->head.eth, "packet smaller than ETH_ZLEN (%d)", ETH_ZLEN);
		goto out;
	}
#endif

	/* a couple more sanity checks */
	BUILD_BUG_ON((unsigned) OMX_PKT_TYPE_MAX != (1<<(sizeof(omx_packet_type_t)*8)) - 1);
	BUILD_BUG_ON(OMX_PKT_TYPE_MAX > 255); /* uint8_t is used on the wire */
	BUILD_BUG_ON(OMX_NACK_TYPE_MAX > 255); /* uint8_t is used on the wire */

	/* get the actual packet type, either from linear data or not */
	if (likely(skb_headlen(skb) >= OMX_HDR_PTYPE_OFFSET + sizeof(ptype))) {
		ptype = mh->body.generic.ptype;
	} else {
		err = skb_copy_bits(skb, OMX_HDR_PTYPE_OFFSET, &ptype, sizeof(ptype));
		if (unlikely(err < 0)) {
			omx_counter_inc(iface, DROP_BAD_HEADER_DATALEN);
			omx_drop_dprintk(&mh->head.eth, "couldn't get packet type");
			goto out;
		}
	}

	/* get the header length */
	hdr_len = omx_pkt_type_hdr_len[ptype];

	/* we need a linear header */
	if (unlikely(skb_headlen(skb) < hdr_len)) {
		/* copy the header in the a linear buffer */
		omx_counter_inc(iface, RECV_NONLINEAR_HEADER);
		err = skb_copy_bits(skb, 0, &linear_header, hdr_len);
		BUG_ON(unlikely(err < 0)); /* there's always at least ETH_ZLEN */
		mh = &linear_header;
	} else {
		/*�the header inside the skb (mh) is already linear */
	}

	/* no need to check ptype since there is a default error handler
	 * for all erroneous values
	 */
	omx_pkt_type_handler[ptype](iface, mh, skb);
	TIMER_STOP(&t_recv);

 out:
	dprintk_out();
	return err;
}

struct packet_type omx_pt = {
	.type = __constant_htons(ETH_P_OMX),
	.func = omx_recv,
};

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
