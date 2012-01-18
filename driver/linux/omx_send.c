/*
 * Open-MX
 * Copyright © inria 2007-2010
 * Copyright © CNRS 2009
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
#include <linux/slab.h>
#include <linux/mm.h>

#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_peer.h"
#include "omx_reg.h"
#include "omx_endpoint.h"
#include "omx_shared.h"

#ifdef OMX_DRIVER_DEBUG
/* defined as module parameters */
extern unsigned long omx_packet_loss;
extern unsigned long omx_TINY_packet_loss;
extern unsigned long omx_SMALL_packet_loss;
extern unsigned long omx_MEDIUM_FRAG_packet_loss;
extern unsigned long omx_RNDV_packet_loss;
extern unsigned long omx_NOTIFY_packet_loss;
extern unsigned long omx_CONNECT_REQUEST_packet_loss;
extern unsigned long omx_CONNECT_REPLY_packet_loss;
extern unsigned long omx_LIBACK_packet_loss;
extern unsigned long omx_NACK_LIB_packet_loss;
extern unsigned long omx_NACK_MCP_packet_loss;
/* index between 0 and the above limit */
unsigned long omx_packet_loss_index = 0;
static unsigned long omx_TINY_packet_loss_index = 0;
static unsigned long omx_SMALL_packet_loss_index = 0;
static unsigned long omx_MEDIUM_FRAG_packet_loss_index = 0;
static unsigned long omx_RNDV_packet_loss_index = 0;
static unsigned long omx_NOTIFY_packet_loss_index = 0;
static unsigned long omx_CONNECT_REQUEST_packet_loss_index = 0;
static unsigned long omx_CONNECT_REPLY_packet_loss_index = 0;
static unsigned long omx_LIBACK_packet_loss_index = 0;
static unsigned long omx_NACK_LIB_packet_loss_index = 0;
static unsigned long omx_NACK_MCP_packet_loss_index = 0;
#endif /* OMX_DRIVER_DEBUG */

/*************************************
 * Allocate and initialize a OMX skb
 */
struct sk_buff *
omx_new_skb(unsigned long len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (likely(skb != NULL)) {
		omx_skb_reset_mac_header(skb);
		omx_skb_reset_network_header(skb);
		skb->protocol = __constant_htons(ETH_P_OMX);
		skb->priority = 0;
		skb_put(skb, len);
		skb->next = skb->prev = NULL;

		/* tell the network layer not to perform IP checksums
		 * or to get the NIC to do it
		 */
		skb->ip_summed = CHECKSUM_NONE;
	}
	return skb;
}

/******************************
 * Deferred event notification
 *
 * When we need to wait for the skb to be completely sent before releasing
 * the resources, we use a skb destructor callback.
 */

struct omx_deferred_event {
	struct omx_endpoint *endpoint;
	struct omx_evt_send_mediumsq_frag_done evt;
};

/* medium frag skb destructor to release sendq pages */
static void
omx_medium_frag_skb_destructor(struct sk_buff *skb)
{
	struct omx_deferred_event * defevent = omx_get_skb_destructor_data(skb);
	struct omx_endpoint * endpoint = defevent->endpoint;

	/* report the event to user-space */
	omx_notify_exp_event(endpoint,
			     &defevent->evt, sizeof(defevent->evt));

	/* release objects now */
	omx_endpoint_release(endpoint);
	kfree(defevent);
}

/*********************
 * Main send routines
 */

#ifdef OMX_DRIVER_DEBUG
static void
omx_tiny_skb_debug_destructor(struct sk_buff *skb)
{
	/* check that nobody modified our destructor data in our back since we queued this skb */
	void * magic =  omx_get_skb_destructor_data(skb);
	WARN_ON(magic != (void *) 0x666);
}
#endif /* OMX_DRIVER_DEBUG */

int
omx_ioctl_send_connect_request(struct omx_endpoint * endpoint,
			       void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_connect *connect_n;
	struct omx_cmd_send_connect_request cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_connect);
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect request cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (!cmd.shared_disabled) {
		ret = omx_shared_try_send_connect_request(endpoint, &cmd);
		if (ret <= 0)
			return ret;
		/* fallback if ret==1 */
	}

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create connect skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	connect_n = (struct omx_pkt_connect *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in connect header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(connect_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(connect_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(connect_n->ptype, OMX_PKT_TYPE_CONNECT);
	OMX_HTON_8(connect_n->length, OMX_PKT_CONNECT_REQUEST_DATA_LENGTH);
	OMX_HTON_16(connect_n->lib_seqnum, cmd.seqnum);
	OMX_HTON_16(connect_n->src_dst_peer_index, cmd.peer_index);
	OMX_HTON_8(connect_n->request.is_reply, 0);
	OMX_HTON_32(connect_n->request.src_session_id, cmd.src_session_id);
	OMX_HTON_32(connect_n->request.app_key, cmd.app_key);
	OMX_HTON_16(connect_n->request.target_recv_seqnum_start, cmd.target_recv_seqnum_start);
	OMX_HTON_8(connect_n->request.connect_seqnum, cmd.connect_seqnum);

	omx_queue_xmit(iface, skb, CONNECT_REQUEST);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_connect_reply(struct omx_endpoint * endpoint,
			     void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_connect *connect_n;
	struct omx_cmd_send_connect_reply cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_connect);
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect reply cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (!cmd.shared_disabled) {
		ret = omx_shared_try_send_connect_reply(endpoint, &cmd);
		if (ret <= 0)
			return ret;
		/* fallback if ret==1 */
	}

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create connect skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	connect_n = (struct omx_pkt_connect *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in connect header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(connect_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(connect_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(connect_n->ptype, OMX_PKT_TYPE_CONNECT);
	OMX_HTON_8(connect_n->length, OMX_PKT_CONNECT_REPLY_DATA_LENGTH);
	OMX_HTON_16(connect_n->lib_seqnum, cmd.seqnum);
	OMX_HTON_16(connect_n->src_dst_peer_index, cmd.peer_index);
	OMX_HTON_8(connect_n->reply.is_reply, 1);
	OMX_HTON_32(connect_n->reply.src_session_id, cmd.src_session_id);
	OMX_HTON_32(connect_n->reply.target_session_id, cmd.target_session_id);
	OMX_HTON_16(connect_n->reply.target_recv_seqnum_start, cmd.target_recv_seqnum_start);
	OMX_HTON_8(connect_n->reply.connect_seqnum, cmd.connect_seqnum);
	OMX_HTON_8(connect_n->reply.connect_status_code, cmd.connect_status_code);

	omx_queue_xmit(iface, skb, CONNECT_REPLY);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_tiny(struct omx_endpoint * endpoint,
		    void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_msg *tiny_n;
	struct omx_cmd_send_tiny_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	char * data;
	int ret;
	uint8_t length;

	ret = copy_from_user(&cmd, &((struct omx_cmd_send_tiny __user *) uparam)->hdr, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (unlikely(length > OMX_TINY_MSG_LENGTH_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as a tiny (tried %d)\n",
		       OMX_TINY_MSG_LENGTH_MAX, length);
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(cmd.shared))
		return omx_shared_send_tiny(endpoint, &cmd, &((struct omx_cmd_send_tiny __user *) uparam)->data);

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create tiny skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	tiny_n = (struct omx_pkt_msg *) (ph + 1);
	data = (char*) (tiny_n + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in tiny header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(tiny_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(tiny_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(tiny_n->ptype, OMX_PKT_TYPE_TINY);
	OMX_HTON_16(tiny_n->length, length);
	OMX_HTON_16(tiny_n->lib_seqnum, cmd.seqnum);
	OMX_HTON_16(tiny_n->lib_piggyack, cmd.piggyack);
	OMX_HTON_32(tiny_n->session, cmd.session_id);
	OMX_HTON_16(tiny_n->checksum, cmd.checksum);
	OMX_HTON_MATCH_INFO(tiny_n, cmd.match_info);

	omx_send_dprintk(eh, "TINY length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(data, &((struct omx_cmd_send_tiny __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

#ifdef OMX_DRIVER_DEBUG
	omx_set_skb_destructor(skb, omx_tiny_skb_debug_destructor, (void *) 0x666);
#endif

	omx_queue_xmit(iface, skb, TINY);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_small(struct omx_endpoint * endpoint,
		     void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_msg *small_n;
	struct omx_cmd_send_small cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	char * data;
	int ret;
	uint32_t length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send small cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	BUILD_BUG_ON(OMX_SMALL_MSG_LENGTH_MAX > OMX_SENDQ_ENTRY_SIZE);

	length = cmd.length;
	if (unlikely(length > OMX_SMALL_MSG_LENGTH_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as a small (tried %d)\n",
		       OMX_SMALL_MSG_LENGTH_MAX, length);
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(cmd.shared))
		return omx_shared_send_small(endpoint, &cmd);

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create small skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	small_n = (struct omx_pkt_msg *) (ph + 1);
	data = (char*) (small_n + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in small header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(small_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(small_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(small_n->ptype, OMX_PKT_TYPE_SMALL);
	OMX_HTON_16(small_n->length, length);
	OMX_HTON_16(small_n->lib_seqnum, cmd.seqnum);
	OMX_HTON_16(small_n->lib_piggyack, cmd.piggyack);
	OMX_HTON_32(small_n->session, cmd.session_id);
	OMX_HTON_16(small_n->checksum, cmd.checksum);
	OMX_HTON_MATCH_INFO(small_n, cmd.match_info);

	omx_send_dprintk(eh, "SMALL length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(data, (__user void *)(unsigned long) cmd.vaddr, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send small cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

	omx_queue_xmit(iface, skb, SMALL);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_mediumsq_frag(struct omx_endpoint * endpoint,
			     void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_medium_frag *medium_n;
	struct omx_cmd_send_mediumsq_frag cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	uint32_t sendq_offset;
	struct page * page;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_medium_frag);
	int ret;
	uint32_t frag_length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send mediumsq frag cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	BUILD_BUG_ON(OMX_MEDIUM_FRAG_LENGTH_MAX > OMX_SENDQ_ENTRY_SIZE);
	BUILD_BUG_ON(OMX_MEDIUM_FRAG_PACKET_SIZE_OF_PAYLOAD(OMX_MEDIUM_FRAG_LENGTH_MAX) > OMX_MTU);

	frag_length = cmd.frag_length;
	if (unlikely(frag_length > OMX_SENDQ_ENTRY_SIZE)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %ld as a mediumsq frag (tried %ld)\n",
		       OMX_SENDQ_ENTRY_SIZE, (unsigned long) frag_length);
		ret = -EINVAL;
		goto out;
	}

	sendq_offset = cmd.sendq_offset;
	if (unlikely(sendq_offset >= OMX_SENDQ_SIZE)) {
		printk(KERN_ERR "Open-MX: Cannot send mediumsq fragment from sendq offset %ld (max %ld)\n",
		       (unsigned long) sendq_offset, (unsigned long) OMX_SENDQ_SIZE);
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(cmd.shared))
		return omx_shared_send_mediumsq_frag(endpoint, &cmd);

	if (unlikely(frag_length > omx_skb_copy_max
		     && hdr_len + frag_length >= ETH_ZLEN
		     && omx_skb_frags >= (frag_length >> OMX_SENDQ_ENTRY_SHIFT))) {
		/* use skb with frags */

		struct omx_deferred_event * defevent;
		unsigned int current_sendq_offset, remaining, desc;

		skb = omx_new_skb(/* only allocate space for the header now, we'll attach pages later */
				   hdr_len);
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			printk(KERN_INFO "Open-MX: Failed to create mediumsq frag skb\n");
			ret = -ENOMEM;
			goto out;
		}

		defevent = kmalloc(sizeof(*defevent), GFP_KERNEL);
		if (unlikely(!defevent)) {
			omx_counter_inc(iface, SEND_NOMEM_MEDIUM_DEFEVENT);
			printk(KERN_INFO "Open-MX: Failed to allocate mediumsq frag deferred event\n");
			ret = -ENOMEM;
			goto out_with_skb;
		}

		/* locate headers */
		mh = omx_skb_mac_header(skb);
		ph = &mh->head;
		eh = &ph->eth;
		medium_n = (struct omx_pkt_medium_frag *) (ph + 1);

		/* set destination peer */
		ret = omx_set_target_peer(ph, iface, cmd.peer_index);
		if (ret < 0) {
			printk(KERN_INFO "Open-MX: Failed to fill target peer in mediumsq frag header\n");
			kfree(defevent);
			goto out_with_skb;
		}

		/* attach the sendq page */
		current_sendq_offset = sendq_offset;
		remaining = frag_length;
		desc = 0;
		while (remaining) {
			unsigned int chunk = remaining;
			if (chunk > PAGE_SIZE)
				chunk = PAGE_SIZE;
			page = endpoint->sendq_pages[current_sendq_offset >> PAGE_SHIFT];
			get_page(page);
			skb_fill_page_desc(skb, desc, page, current_sendq_offset & (~PAGE_MASK), chunk);
			desc++;
			remaining -= chunk;
			current_sendq_offset += chunk;
		}
		skb->len += frag_length;
		skb->data_len = frag_length;

		/* prepare the deferred event now that we cannot fail anymore */
		omx_endpoint_reacquire(endpoint); /* keep a reference in the defevent */
		defevent->endpoint = endpoint;
		defevent->evt.id = 0;
		defevent->evt.type = OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE;
		defevent->evt.sendq_offset = cmd.sendq_offset;
		omx_set_skb_destructor(skb, omx_medium_frag_skb_destructor, defevent);

	} else {
		/* use a linear skb */
		struct omx_evt_send_mediumsq_frag_done evt;
		void *data;

		omx_counter_inc(iface, MEDIUMSQ_FRAG_SEND_LINEAR);

		skb = omx_new_skb(/* pad to ETH_ZLEN */
				  max_t(unsigned long, hdr_len + frag_length, ETH_ZLEN));
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			printk(KERN_INFO "Open-MX: Failed to create linear mediumsq frag skb\n");
			ret = -ENOMEM;
			goto out;
		}

		/* locate headers */
		mh = omx_skb_mac_header(skb);
		ph = &mh->head;
		eh = &ph->eth;
		medium_n = (struct omx_pkt_medium_frag *) (ph + 1);
		data = (char*) (medium_n + 1);

		/* set destination peer */
		ret = omx_set_target_peer(ph, iface, cmd.peer_index);
		if (ret < 0) {
			printk(KERN_INFO "Open-MX: Failed to fill target peer in mediumsq frag header\n");
			goto out_with_skb;
		}

		/* copy the data in the linear skb */
		memcpy(data, endpoint->sendq + sendq_offset, frag_length);

		/* notify the event right now */
		evt.id = 0;
		evt.type = OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE;
		evt.sendq_offset = cmd.sendq_offset;
		omx_notify_exp_event(endpoint,
				     &evt, sizeof(evt));
	}

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* fill omx header */
	OMX_HTON_8(medium_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(medium_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(medium_n->ptype, OMX_PKT_TYPE_MEDIUM);
#ifdef OMX_MX_WIRE_COMPAT
	OMX_HTON_16(medium_n->length, cmd.msg_length);
	OMX_HTON_8(medium_n->frag_pipeline, cmd.frag_pipeline);
#else
	OMX_HTON_32(medium_n->length, cmd.msg_length);
#endif
	OMX_HTON_16(medium_n->lib_seqnum, cmd.seqnum);
	OMX_HTON_16(medium_n->lib_piggyack, cmd.piggyack);
	OMX_HTON_32(medium_n->session, cmd.session_id);
	OMX_HTON_MATCH_INFO(medium_n, cmd.match_info);
	OMX_HTON_16(medium_n->frag_length, frag_length);
	OMX_HTON_8(medium_n->frag_seqnum, cmd.frag_seqnum);
	OMX_HTON_16(medium_n->checksum, cmd.checksum);

	omx_send_dprintk(eh, "MEDIUMSQ FRAG length %ld", (unsigned long) frag_length);

	_omx_queue_xmit(iface, skb, MEDIUM_FRAG, MEDIUMSQ_FRAG);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_mediumva(struct omx_endpoint * endpoint,
			void __user * uparam)
{
	struct omx_cmd_send_mediumva cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct sk_buff *skb;
	struct omx_cmd_user_segment *usegs, *cur_useg;
	uint32_t msg_length, remaining, cur_useg_remaining;
	void __user * cur_udata;
	uint32_t nseg;
	int ret;
	int frags_nr;
     	int i;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send mediumva cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	msg_length = cmd.length;
#ifdef OMX_MX_WIRE_COMPAT
	if (unlikely(msg_length > OMX__MX_MEDIUM_MSG_LENGTH_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %ld as a mediumva in MX-wire-compat mode (tried %ld)\n",
		       (unsigned long) OMX__MX_MEDIUM_MSG_LENGTH_MAX, (unsigned long) msg_length);
		ret = -EINVAL;
		goto out;
	}
#endif
	frags_nr = (msg_length+OMX_MEDIUM_FRAG_LENGTH_MAX-1) / OMX_MEDIUM_FRAG_LENGTH_MAX;
	nseg = cmd.nr_segments;

	if (unlikely(cmd.shared))
		return omx_shared_send_mediumva(endpoint, &cmd);

	/* get user segments */
	usegs = kmalloc(nseg * sizeof(struct omx_cmd_user_segment), GFP_KERNEL);
	if ( !usegs) {
		printk(KERN_ERR "Open-MX: Cannot allocate segments for mediumva\n");
		ret = -ENOMEM;
		goto out;
	}
	ret = copy_from_user(usegs, (void __user *)(unsigned long) cmd.segments,
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

	for(i=0; i<frags_nr; i++) {
		struct omx_hdr *mh;
		struct omx_pkt_head *ph;
		struct ethhdr *eh;
		struct omx_pkt_medium_frag *medium_n;
		size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_medium_frag);
		uint16_t frag_length = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX ? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
		uint16_t frag_remaining = frag_length;
		void *data;

		skb = omx_new_skb(/* pad to ETH_ZLEN */
				  max_t(unsigned long, hdr_len + frag_length, ETH_ZLEN));
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			printk(KERN_INFO "Open-MX: Failed to create linear mediumva skb\n");
			ret = -ENOMEM;
			goto out_with_usegs;
		}

		/* locate headers */
		mh = omx_skb_mac_header(skb);
		ph = &mh->head;
		eh = &ph->eth;
		medium_n = (struct omx_pkt_medium_frag *) (ph + 1);
		data = (char*) (medium_n + 1);

		/* set destination peer */
		ret = omx_set_target_peer(ph, iface, cmd.peer_index);
		if (ret < 0) {
			printk(KERN_INFO "Open-MX: Failed to fill target peer in medium sendq frag header\n");
			goto out_with_skb;
		}

		/* fill ethernet header */
		eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
		memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

		/* fill omx header */
		OMX_HTON_8(medium_n->src_endpoint, endpoint->endpoint_index);
		OMX_HTON_8(medium_n->dst_endpoint, cmd.dest_endpoint);
		OMX_HTON_8(medium_n->ptype, OMX_PKT_TYPE_MEDIUM);
#ifdef OMX_MX_WIRE_COMPAT
		OMX_HTON_16(medium_n->length, msg_length);
		OMX_HTON_8(medium_n->frag_pipeline, OMX_MEDIUM_FRAG_LENGTH_SHIFT);
#else
		OMX_HTON_32(medium_n->length, msg_length);
#endif
		OMX_HTON_16(medium_n->lib_seqnum, cmd.seqnum);
		OMX_HTON_16(medium_n->lib_piggyack, cmd.piggyack);
		OMX_HTON_32(medium_n->session, cmd.session_id);
		OMX_HTON_MATCH_INFO(medium_n, cmd.match_info);
		OMX_HTON_16(medium_n->frag_length, frag_length);
		OMX_HTON_8(medium_n->frag_seqnum, i);

		omx_send_dprintk(eh, "MEDIUMVA length %ld", (unsigned long) frag_length);

		/* copy the data right after the header */
		while (frag_remaining) {
			uint16_t chunk = frag_remaining > cur_useg_remaining ? cur_useg_remaining : frag_remaining;
			ret = copy_from_user(data, cur_udata, chunk);
			if (unlikely(ret != 0)) {
				printk(KERN_ERR "Open-MX: Failed to read send mediumva cmd data\n");
				ret = -EFAULT;
				goto out_with_skb;
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
			data += chunk;
		}
		remaining -= frag_length;

		_omx_queue_xmit(iface, skb, MEDIUM_FRAG, MEDIUMVA_FRAG);
	}

	kfree(usegs);
	return 0;

 out_with_skb:
	kfree_skb(skb);
 out_with_usegs:
	kfree(usegs);
 out:
	return ret;
}

int
omx_ioctl_send_rndv(struct omx_endpoint * endpoint,
		    void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_rndv *rndv_n;
	struct omx_cmd_send_rndv cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_rndv);
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send rndv cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.shared))
		return omx_shared_send_rndv(endpoint, &cmd);

	if (!omx_pin_synchronous) {
		/* make sure the region is pinned */
		struct omx_user_region * region;
		struct omx_user_region_pin_state pinstate;

		region = omx_user_region_acquire(endpoint, cmd.pulled_rdma_id);
		if (unlikely(!region)) {
			ret = -EINVAL;
			goto out;
		}

		omx_user_region_demand_pin_init(&pinstate, region);
		pinstate.next_chunk_pages = omx_pin_chunk_pages_max;
		ret = omx_user_region_demand_pin_finish(&pinstate);
		/* no progressive/demand-pinning for native networking */
		omx_user_region_release(region);
		if (ret < 0) {
			dprintk(REG, "failed to pin user region\n");
			goto out;
		}
	}

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create rndv skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	rndv_n = (struct omx_pkt_rndv *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in rndv header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(rndv_n->msg.src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(rndv_n->msg.dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(rndv_n->msg.ptype, OMX_PKT_TYPE_RNDV);
	OMX_HTON_16(rndv_n->msg.length, OMX_PKT_RNDV_DATA_LENGTH);
	OMX_HTON_16(rndv_n->msg.lib_seqnum, cmd.seqnum);
	OMX_HTON_16(rndv_n->msg.lib_piggyack, cmd.piggyack);
	OMX_HTON_32(rndv_n->msg.session, cmd.session_id);
	OMX_HTON_MATCH_INFO(&rndv_n->msg, cmd.match_info);
	OMX_HTON_32(rndv_n->msg_length, cmd.msg_length);
	OMX_HTON_8(rndv_n->pulled_rdma_id, cmd.pulled_rdma_id);
	OMX_HTON_8(rndv_n->pulled_rdma_seqnum, cmd.pulled_rdma_seqnum);
	OMX_HTON_16(rndv_n->msg.checksum, cmd.checksum);
	OMX_HTON_16(rndv_n->pulled_rdma_offset, 0); /* not needed for Open-MX */

	omx_queue_xmit(iface, skb, RNDV);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_notify(struct omx_endpoint * endpoint,
		      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_notify *notify_n;
	struct omx_cmd_send_notify cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_notify);
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send notify cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.shared))
		return omx_shared_send_notify(endpoint, &cmd);

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create notify skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	notify_n = (struct omx_pkt_notify *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(notify_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(notify_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(notify_n->ptype, OMX_PKT_TYPE_NOTIFY);
	OMX_HTON_32(notify_n->total_length, cmd.total_length);
	OMX_HTON_16(notify_n->lib_seqnum, cmd.seqnum);
	OMX_HTON_16(notify_n->lib_piggyack, cmd.piggyack);
	OMX_HTON_32(notify_n->session, cmd.session_id);
	OMX_HTON_8(notify_n->pulled_rdma_id, cmd.pulled_rdma_id);
	OMX_HTON_8(notify_n->pulled_rdma_seqnum, cmd.pulled_rdma_seqnum);

	omx_send_dprintk(eh, "NOTIFY");

	omx_queue_xmit(iface, skb, NOTIFY);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_liback(struct omx_endpoint * endpoint,
		      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_truc *truc_n;
	struct omx_cmd_send_liback cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_truc);
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send truc cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.shared))
		return omx_shared_send_liback(endpoint, &cmd);

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create truc skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	truc_n = (struct omx_pkt_truc *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in truc header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(truc_n->src_endpoint, endpoint->endpoint_index);
	OMX_HTON_8(truc_n->dst_endpoint, cmd.dest_endpoint);
	OMX_HTON_8(truc_n->ptype, OMX_PKT_TYPE_TRUC);
	OMX_HTON_8(truc_n->length, OMX_PKT_TRUC_LIBACK_DATA_LENGTH);
	OMX_HTON_32(truc_n->session, cmd.session_id);
	OMX_HTON_8(truc_n->type, OMX_PKT_TRUC_DATA_TYPE_ACK);
	OMX_HTON_16(truc_n->liback.lib_seqnum, cmd.lib_seqnum);
	OMX_HTON_32(truc_n->liback.session_id, cmd.session_id);
	OMX_HTON_32(truc_n->liback.acknum, cmd.acknum);
	OMX_HTON_16(truc_n->liback.send_seq, cmd.send_seq);
	OMX_HTON_8(truc_n->liback.resent, cmd.resent);

	omx_queue_xmit(iface, skb, LIBACK);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

void
omx_send_nack_lib(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type,
		  uint8_t src_endpoint, uint8_t dst_endpoint, uint16_t lib_seqnum)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_nack_lib *nack_lib_n;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_nack_lib);
	int ret;

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create nack lib skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	nack_lib_n = (struct omx_pkt_nack_lib *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		/* FIXME: BUG? */
		goto out_with_skb;
	}
	nack_lib_n->dst_src_peer_index = ph->dst_src_peer_index;

	/* fill omx header */
	OMX_HTON_8(nack_lib_n->src_endpoint, src_endpoint);
	OMX_HTON_8(nack_lib_n->dst_endpoint, dst_endpoint);
	OMX_HTON_8(nack_lib_n->ptype, OMX_PKT_TYPE_NACK_LIB);
	OMX_HTON_8(nack_lib_n->nack_type, nack_type);
	OMX_HTON_16(nack_lib_n->lib_seqnum, lib_seqnum);

	omx_send_dprintk(eh, "NACK LIB type %d", nack_type);

	omx_queue_xmit(iface, skb, NACK_LIB);

	return;

 out_with_skb:
	kfree_skb(skb);
 out:
	/* just forget about it, it will be resent anyway */
	/* return ret; */
	return;
}

void
omx_send_nack_mcp(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type,
		  uint8_t src_endpoint, uint32_t src_pull_handle, uint32_t src_magic)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_nack_mcp *nack_mcp_n;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_nack_mcp);
	int ret;

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		omx_counter_inc(iface, SEND_NOMEM_SKB);
		printk(KERN_INFO "Open-MX: Failed to create nack mcp skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	nack_mcp_n = (struct omx_pkt_nack_mcp *) (ph + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, iface, peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		/* FIXME: BUG? */
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_HTON_8(nack_mcp_n->src_endpoint, src_endpoint);
	OMX_HTON_8(nack_mcp_n->ptype, OMX_PKT_TYPE_NACK_MCP);
	OMX_HTON_8(nack_mcp_n->nack_type, nack_type);
	OMX_HTON_32(nack_mcp_n->src_pull_handle, src_pull_handle);
	OMX_HTON_32(nack_mcp_n->src_magic, src_magic);

	omx_send_dprintk(eh, "NACK MCP type %d", nack_type);

	omx_queue_xmit(iface, skb, NACK_MCP);

	return;

 out_with_skb:
	kfree_skb(skb);
 out:
	/* just forget about it, it will be resent anyway */
	/* return ret; */
	return;
}

/*
 * Command to benchmark commands
 */
int
omx_ioctl_bench(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct omx_cmd_bench_hdr cmd;
	union omx_evt event;
	char data[OMX_TINY_MSG_LENGTH_MAX];
	int ret = 0;

	/* level 00: only pass the command and get the endpoint */
	if (!uparam)
		goto out;

	ret = copy_from_user(&cmd, &((struct omx_cmd_bench __user *) uparam)->hdr, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	/* level 01: get command parameters from users-space */
	if (cmd.type == OMX_CMD_BENCH_TYPE_PARAMS)
		goto out;

	skb = omx_new_skb(ETH_ZLEN);
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create bench skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* level 02: alloc skb */
	if (cmd.type == OMX_CMD_BENCH_TYPE_SEND_ALLOC)
		goto out_with_skb;

	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, (uint64_t)-1ULL);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* level 03: prepare */
	if (cmd.type == OMX_CMD_BENCH_TYPE_SEND_PREP)
		goto out_with_skb;

	ret = copy_from_user(data, &((struct omx_cmd_bench __user *) uparam)->dummy_data, OMX_TINY_MSG_LENGTH_MAX);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

	/* level 04: fill */
	if (cmd.type == OMX_CMD_BENCH_TYPE_SEND_FILL)
		goto out_with_skb;

	skb->dev = iface->eth_ifp;
	dev_queue_xmit(skb); /* no need to use omx_queue_xmit here */

	/* level 05: send done */
	if (cmd.type == OMX_CMD_BENCH_TYPE_SEND_DONE)
		goto out;

	endpoint = omx_endpoint_acquire_by_iface_index(iface, endpoint->endpoint_index);
	BUG_ON(IS_ERR(endpoint));

	/* level 11: recv acquire */
	if (cmd.type == OMX_CMD_BENCH_TYPE_RECV_ACQU)
		goto out_with_endpoint;

	event.generic.id = 0;
	event.generic.type = OMX_EVT_NONE;
	omx_notify_exp_event(endpoint, &event, 0);

	/* level 12: recv notify */
	if (cmd.type == OMX_CMD_BENCH_TYPE_RECV_NOTIFY)
		goto out_with_endpoint;

	omx_endpoint_release(endpoint);

	/* level 13: recv done */
	if (cmd.type == OMX_CMD_BENCH_TYPE_RECV_DONE)
		goto out;

	return -EINVAL;

 out_with_endpoint:
	omx_endpoint_release(endpoint);
	goto out;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
