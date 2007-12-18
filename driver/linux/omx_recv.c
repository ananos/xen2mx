/*
 * Open-MX
 * Copyright Â© INRIA 2007 (see AUTHORS file)
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

#include "omx_common.h"
#include "omx_hal.h"
#include "omx_wire_access.h"

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
	uint32_t peer_index;
	struct omx_pkt_connect *connect_n = &mh->body.connect;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_connect);
	uint8_t length = OMX_FROM_PKT_FIELD(connect_n->length);
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(connect_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(connect_n->src_endpoint);
	uint16_t reverse_peer_index = OMX_FROM_PKT_FIELD(connect_n->src_dst_peer_index);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(connect_n->lib_seqnum);
	struct omx_evt_recv_connect event;
	int err = 0;

	/* check packet length */
	if (unlikely(length > OMX_CONNECT_DATA_MAX)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_DATALEN);
		omx_drop_dprintk(eh, "CONNECT packet data too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SKBLEN);
		omx_drop_dprintk(eh, "CONNECT packet data with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* the connect doesn't know its peer index tet, we need to lookup */
	err = omx_peer_lookup_by_addr(src_addr, NULL, &peer_index);
	if (err < 0) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(eh, "CONNECT packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* store our peer_index in the remote table */
	err = omx_peer_set_reverse_index(peer_index, reverse_peer_index);
	BUG_ON(err < 0);

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "CONNECT packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	omx_recv_dprintk(eh, "CONNECT data length %ld", (unsigned long) length);

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.length = length;
	event.seqnum = lib_seqnum;

	/* copy data in event data */
	err = skb_copy_bits(skb, hdr_len, event.data, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, OMX_EVT_RECV_CONNECT, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(eh, "CONNECT packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_tiny(struct omx_iface * iface,
	      struct omx_hdr * mh,
	      struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_msg *tiny_n = &mh->body.tiny;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	uint16_t length = OMX_FROM_PKT_FIELD(tiny_n->length);
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(tiny_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(tiny_n->src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(tiny_n->session);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(tiny_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_FROM_PKT_FIELD(tiny_n->lib_piggyack);
	struct omx_evt_recv_msg event;
	int err = 0;

	/* check packet length */
	if (unlikely(length > OMX_TINY_MAX)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_DATALEN);
		omx_drop_dprintk(&mh->head.eth, "TINY packet too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SKBLEN);
		omx_drop_dprintk(&mh->head.eth, "TINY packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(&mh->head.eth, "TINY packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "TINY packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SESSION);
		omx_drop_dprintk(&mh->head.eth, "TINY packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(&mh->head.eth, "TINY length %ld", (unsigned long) length);

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_FROM_PKT_MATCH_INFO(tiny_n);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.tiny.length = length;

	/* copy data in event data */
	err = skb_copy_bits(skb, hdr_len, event.specific.tiny.data, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, OMX_EVT_RECV_TINY, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(&mh->head.eth, "TINY packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_small(struct omx_iface * iface,
	       struct omx_hdr * mh,
	       struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_msg *small_n = &mh->body.small;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	uint16_t length =  OMX_FROM_PKT_FIELD(small_n->length);
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(small_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(small_n->src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(small_n->session);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(small_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_FROM_PKT_FIELD(small_n->lib_piggyack);
	struct omx_evt_recv_msg event;
	unsigned long recvq_offset;
	int err;

	/* check packet length */
	if (unlikely(length > OMX_SMALL_MAX)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_DATALEN);
		omx_drop_dprintk(&mh->head.eth, "SMALL packet too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SKBLEN);
		omx_drop_dprintk(&mh->head.eth, "SMALL packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(&mh->head.eth, "SMALL packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "SMALL packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SESSION);
		omx_drop_dprintk(&mh->head.eth, "SMALL packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* get the eventq slot */
	err = omx_prepare_notify_unexp_event_with_recvq(endpoint, &recvq_offset);
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(&mh->head.eth, "SMALL packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_FROM_PKT_MATCH_INFO(small_n);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.small.length = length;
	event.specific.small.recvq_offset = recvq_offset;

	omx_recv_dprintk(&mh->head.eth, "SMALL length %ld", (unsigned long) length);

	/* copy data in recvq slot */
	err = skb_copy_bits(skb, hdr_len, endpoint->recvq + recvq_offset, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* notify the event */
	omx_commit_notify_unexp_event_with_recvq(endpoint, OMX_EVT_RECV_SMALL, &event, sizeof(event));

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_medium_frag(struct omx_iface * iface,
		     struct omx_hdr * mh,
		     struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_medium_frag *medium_n = &mh->body.medium;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_medium_frag);
	uint16_t frag_length = OMX_FROM_PKT_FIELD(medium_n->frag_length);
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(medium_n->msg.dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(medium_n->msg.src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(medium_n->msg.session);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(medium_n->msg.lib_seqnum);
	uint16_t lib_piggyack = OMX_FROM_PKT_FIELD(medium_n->msg.lib_piggyack);
	struct omx_evt_recv_msg event;
	unsigned long recvq_offset;
	int err;

	/* check packet length */
	if (unlikely(frag_length > OMX_RECVQ_ENTRY_SIZE)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_DATALEN);
		omx_drop_dprintk(&mh->head.eth, "MEDIUM fragment packet too long (length %d)",
				 (unsigned) frag_length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(frag_length > skb->len - hdr_len)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SKBLEN);
		omx_drop_dprintk(&mh->head.eth, "MEDIUM fragment with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) frag_length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(&mh->head.eth, "MEDIUM packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "MEDIUM packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SESSION);
		omx_drop_dprintk(&mh->head.eth, "MEDIUM packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	/* get the eventq slot */
	err = omx_prepare_notify_unexp_event_with_recvq(endpoint, &recvq_offset);
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(&mh->head.eth, "MEDIUM packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_FROM_PKT_MATCH_INFO(&medium_n->msg);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.medium.msg_length = OMX_FROM_PKT_FIELD(medium_n->msg.length);
	event.specific.medium.frag_length = frag_length;
	event.specific.medium.frag_seqnum = OMX_FROM_PKT_FIELD(medium_n->frag_seqnum);
	event.specific.medium.frag_pipeline = OMX_FROM_PKT_FIELD(medium_n->frag_pipeline);
	event.specific.medium.recvq_offset = recvq_offset;

	omx_recv_dprintk(&mh->head.eth, "MEDIUM_FRAG length %ld", (unsigned long) frag_length);

	/* copy data in recvq slot */
	err = skb_copy_bits(skb, hdr_len, endpoint->recvq + recvq_offset, frag_length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* notify the event */
	omx_commit_notify_unexp_event_with_recvq(endpoint, OMX_EVT_RECV_MEDIUM, &event, sizeof(event));

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_rndv(struct omx_iface * iface,
	      struct omx_hdr * mh,
	      struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_msg *rndv_n = &mh->body.rndv;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	uint16_t length = OMX_FROM_PKT_FIELD(rndv_n->length);
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(rndv_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(rndv_n->src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(rndv_n->session);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(rndv_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_FROM_PKT_FIELD(rndv_n->lib_piggyack);
	struct omx_evt_recv_msg event;
	int err = 0;

	/* check packet length */
	if (unlikely(length > OMX_RNDV_DATA_MAX)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_DATALEN);
		omx_drop_dprintk(&mh->head.eth, "RNDV packet too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SKBLEN);
		omx_drop_dprintk(&mh->head.eth, "RNDV packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(&mh->head.eth, "RNDV packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "RNDV packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SESSION);
		omx_drop_dprintk(&mh->head.eth, "RNDV packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(&mh->head.eth, "RNDV length %ld", (unsigned long) length);

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.match_info = OMX_FROM_PKT_MATCH_INFO(rndv_n);
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.rndv.length = length;

	/* copy data in event data */
	err = skb_copy_bits(skb, hdr_len, event.specific.rndv.data, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, OMX_EVT_RECV_RNDV, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(&mh->head.eth, "RNDV packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_notify(struct omx_iface * iface,
		struct omx_hdr * mh,
		struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_notify *notify_n = &mh->body.notify;
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(notify_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(notify_n->src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(notify_n->session);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(notify_n->lib_seqnum);
	uint16_t lib_piggyack = OMX_FROM_PKT_FIELD(notify_n->lib_piggyack);
	struct omx_evt_recv_msg event;
	int err = 0;

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(&mh->head.eth, "NOTIFY packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "NOTIFY packet for unknown endpoint %d",
				 dst_endpoint);
		omx_send_nack_lib(iface, peer_index,
				  omx_endpoint_acquire_by_iface_index_error_to_nack_type(endpoint),
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SESSION);
		omx_drop_dprintk(&mh->head.eth, "NOTIFY packet with bad session");
		omx_send_nack_lib(iface, peer_index,
				  OMX_NACK_TYPE_BAD_SESSION,
				  dst_endpoint, src_endpoint, lib_seqnum);
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(&mh->head.eth, "NOTIFY");

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.seqnum = lib_seqnum;
	event.piggyack = lib_piggyack;
	event.specific.notify.length = OMX_FROM_PKT_FIELD(notify_n->total_length);
	event.specific.notify.puller_rdma_id = OMX_FROM_PKT_FIELD(notify_n->puller_rdma_id);
	event.specific.notify.puller_rdma_seqnum = OMX_FROM_PKT_FIELD(notify_n->puller_rdma_seqnum);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, OMX_EVT_RECV_NOTIFY, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(&mh->head.eth, "NOTIFY packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_truc(struct omx_iface * iface,
	      struct omx_hdr * mh,
	      struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_truc *truc_n = &mh->body.truc;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_truc);
	uint8_t length = OMX_FROM_PKT_FIELD(truc_n->length);
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(truc_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(truc_n->src_endpoint);
	uint32_t session_id = OMX_FROM_PKT_FIELD(truc_n->session);
	struct omx_evt_recv_truc event;
	int err = 0;

	/* check packet length */
	if (unlikely(length > OMX_TRUC_DATA_MAX)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_DATALEN);
		omx_drop_dprintk(&mh->head.eth, "TRUC packet too long (length %d)",
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (unlikely(length > skb->len - hdr_len)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SKBLEN);
		omx_drop_dprintk(&mh->head.eth, "TRUC packet with %ld bytes instead of %d",
				 (unsigned long) skb->len - hdr_len,
				 (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_PEER_INDEX);
		omx_drop_dprintk(&mh->head.eth, "TRUC packet with unknown peer index %d",
				 (unsigned) peer_index);
		goto out;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(&mh->head.eth, "TRUC packet for unknown endpoint %d",
				 dst_endpoint);
		/* no nack for truc messages, just drop */
		err = PTR_ERR(endpoint);
		goto out;
	}

	/* check the session */
	if (unlikely(session_id != endpoint->session_id)) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_SESSION);
		omx_drop_dprintk(&mh->head.eth, "TRUC packet with bad session");
		/* no nack for truc messages, just drop */
		err = -EINVAL;
		goto out_with_endpoint;
	}

	omx_recv_dprintk(&mh->head.eth, "TRUC length %ld", (unsigned long) length);

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.length = length;

	/* copy data in event data */
	err = skb_copy_bits(skb, hdr_len, event.data, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, OMX_EVT_RECV_TRUC, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(&mh->head.eth, "TRUC packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_nack_lib(struct omx_iface * iface,
		  struct omx_hdr * mh,
		  struct sk_buff * skb)
{
	struct omx_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	uint16_t peer_index = OMX_FROM_PKT_FIELD(mh->head.dst_src_peer_index);
	struct omx_pkt_nack_lib *nack_lib_n = &mh->body.nack_lib;
	uint8_t dst_endpoint = OMX_FROM_PKT_FIELD(nack_lib_n->dst_endpoint);
	uint8_t src_endpoint = OMX_FROM_PKT_FIELD(nack_lib_n->src_endpoint);
	enum omx_nack_type nack_type = OMX_FROM_PKT_FIELD(nack_lib_n->nack_type);
	uint16_t lib_seqnum = OMX_FROM_PKT_FIELD(nack_lib_n->lib_seqnum);
	struct omx_evt_recv_nack_lib event;
	int err = 0;

	/* check the peer index */
	err = omx_check_recv_peer_index(peer_index);
	if (unlikely(err < 0)) {
		/* FIXME: impossible? in non MX-wire compatible only? */
		uint32_t src_addr_peer_index;
		uint64_t src_addr;

		if (peer_index != (uint16_t)-1) {
			omx_drop_dprintk(eh, "NACK LIB with bad peer index %d",
					 (unsigned) peer_index);
			goto out;
		}

		src_addr = omx_board_addr_from_ethhdr_src(eh);
		err = omx_peer_lookup_by_addr(src_addr, NULL, &src_addr_peer_index);
		if (err < 0) {
			omx_drop_dprintk(eh, "NACK LIB with unknown peer index and unknown address");
			goto out;
		}

		peer_index = src_addr_peer_index;
	}

	/* get the destination endpoint */
	endpoint = omx_endpoint_acquire_by_iface_index(iface, dst_endpoint);
	if (unlikely(IS_ERR(endpoint))) {
		omx_counter_inc(iface, OMX_COUNTER_DROP_BAD_ENDPOINT);
		omx_drop_dprintk(eh, "NACK LIB packet for unknown endpoint %d",
				 dst_endpoint);
		/* FIXME: BUG? */
		err = PTR_ERR(endpoint);
		goto out;
	}

	omx_recv_dprintk(eh, "NACK LIB type %s",
			 omx_strnacktype(nack_type));

	/* fill event */
	event.peer_index = peer_index;
	event.src_endpoint = src_endpoint;
	event.seqnum = lib_seqnum;
	event.nack_type = nack_type; /* types are different, values are the same */

	/* notify the event */
	err = omx_notify_unexp_event(endpoint, OMX_EVT_RECV_NACK_LIB, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		omx_counter_inc(iface, OMX_COUNTER_DROP_UNEXP_EVENTQ_FULL);
		omx_drop_dprintk(eh, "NACK LIB packet because of unexpected event queue full");
		goto out_with_endpoint;
	}

	omx_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
 out:
	return err;
}

static int
omx_recv_nosys(struct omx_iface * iface,
		struct omx_hdr * mh,
		struct sk_buff * skb)
{
	omx_counter_inc(iface, OMX_COUNTER_DROP_NOSYS_TYPE);
	omx_drop_dprintk(&mh->head.eth, "packet with unsupported type %d",
			 mh->body.generic.ptype);

	return 0;
}

static int
omx_recv_error(struct omx_iface * iface,
		struct omx_hdr * mh,
		struct sk_buff * skb)
{
	omx_counter_inc(iface, OMX_COUNTER_DROP_UNKNOWN_TYPE);
	omx_drop_dprintk(&mh->head.eth, "packet with unrecognized type %d",
			 mh->body.generic.ptype);

	return 0;
}

/***********************
 * Packet type handlers
 */

static int (*omx_pkt_type_handlers[OMX_PKT_TYPE_MAX+1])(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);

void
omx_pkt_type_handlers_init(void)
{
	int i;

	for(i=0; i<=OMX_PKT_TYPE_MAX; i++)
		omx_pkt_type_handlers[i] = omx_recv_error;

	omx_pkt_type_handlers[OMX_PKT_TYPE_RAW] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_MFM_NIC_REPLY] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_HOST_QUERY] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_HOST_REPLY] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_ETHER_UNICAST] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_ETHER_MULTICAST] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_ETHER_NATIVE] = omx_recv_nosys; /* FIXME */
	omx_pkt_type_handlers[OMX_PKT_TYPE_TRUC] = omx_recv_truc;
	omx_pkt_type_handlers[OMX_PKT_TYPE_CONNECT] = omx_recv_connect;
	omx_pkt_type_handlers[OMX_PKT_TYPE_TINY] = omx_recv_tiny;
	omx_pkt_type_handlers[OMX_PKT_TYPE_SMALL] = omx_recv_small;
	omx_pkt_type_handlers[OMX_PKT_TYPE_MEDIUM] = omx_recv_medium_frag;
	omx_pkt_type_handlers[OMX_PKT_TYPE_RNDV] = omx_recv_rndv;
	omx_pkt_type_handlers[OMX_PKT_TYPE_PULL] = omx_recv_pull;
	omx_pkt_type_handlers[OMX_PKT_TYPE_PULL_REPLY] = omx_recv_pull_reply;
	omx_pkt_type_handlers[OMX_PKT_TYPE_NOTIFY] = omx_recv_notify;
	omx_pkt_type_handlers[OMX_PKT_TYPE_NACK_LIB] = omx_recv_nack_lib;
	omx_pkt_type_handlers[OMX_PKT_TYPE_NACK_MCP] = omx_recv_nack_mcp;
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

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(skb == NULL))
		return 0;

	/* len doesn't include header */
	skb_push(skb, ETH_HLEN);

	iface = omx_iface_find_by_ifp(ifp);
	if (unlikely(!iface)) {
		/* at least the ethhdr is linear in the skb */
		omx_drop_dprintk(&omx_hdr(skb)->head.eth, "packet on non-Open-MX interface %s",
				 ifp->name);
		goto out;
	}

	/* no need to linearize the whole skb,
	 * but at least the header to make things simple */
	if (unlikely(skb_headlen(skb) < sizeof(struct omx_hdr))) {
		skb_copy_bits(skb, 0, &linear_header,
			      sizeof(struct omx_hdr));
		/* check for EFAULT */
		mh = &linear_header;
	} else {
		/* no need to linearize the header */
		mh = omx_hdr(skb);
	}

	/* no need to check ptype since there is a default error handler
	 * for all erroneous values
	 */
	omx_pkt_type_handlers[mh->body.generic.ptype](iface, mh, skb);

 out:
	/* FIXME: send nack */
	dev_kfree_skb(skb);
	return 0;
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
