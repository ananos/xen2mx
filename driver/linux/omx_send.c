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
#include <linux/skbuff.h>
#include <linux/mm.h>

#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_peer.h"
#include "omx_reg.h"
#include "omx_endpoint.h"
#ifndef OMX_DISABLE_SHARED
#include "omx_shared.h"
#endif

#ifdef OMX_DRIVER_DEBUG
/* defined as module parameters */
extern unsigned long omx_TINY_packet_loss;
extern unsigned long omx_SMALL_packet_loss;
extern unsigned long omx_MEDIUM_FRAG_packet_loss;
extern unsigned long omx_RNDV_packet_loss;
extern unsigned long omx_NOTIFY_packet_loss;
extern unsigned long omx_CONNECT_packet_loss;
extern unsigned long omx_TRUC_packet_loss;
extern unsigned long omx_NACK_LIB_packet_loss;
extern unsigned long omx_NACK_MCP_packet_loss;
/* index between 0 and the above limit */
static unsigned long omx_TINY_packet_loss_index = 0;
static unsigned long omx_SMALL_packet_loss_index = 0;
static unsigned long omx_MEDIUM_FRAG_packet_loss_index = 0;
static unsigned long omx_RNDV_packet_loss_index = 0;
static unsigned long omx_NOTIFY_packet_loss_index = 0;
static unsigned long omx_CONNECT_packet_loss_index = 0;
static unsigned long omx_TRUC_packet_loss_index = 0;
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
	struct omx_evt_send_medium_frag_done evt;
};

/* medium frag skb destructor to release sendq pages */
static void
omx_medium_frag_skb_destructor(struct sk_buff *skb)
{
	struct omx_deferred_event * defevent = omx_get_skb_destructor_data(skb);
	struct omx_endpoint * endpoint = defevent->endpoint;

	/* report the event to user-space */
	omx_notify_exp_event(endpoint,
			     OMX_EVT_SEND_MEDIUM_FRAG_DONE,
			     &defevent->evt, sizeof(defevent->evt));

	/* release objects now */
	omx_endpoint_release(endpoint);
	kfree(defevent);
}

/*********************
 * Main send routines
 */

int
omx_ioctl_send_connect(struct omx_endpoint * endpoint,
		       void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_connect *connect_n;
	struct omx_cmd_send_connect_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_connect);
	char * data;
	int ret;
	uint8_t length;

	ret = copy_from_user(&cmd, &((struct omx_cmd_send_connect __user *) uparam)->hdr, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (unlikely(length > OMX_CONNECT_DATA_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as connect data (tried %d)\n",
		       OMX_CONNECT_DATA_MAX, length);
		ret = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (!cmd.shared_disabled) {
		ret = omx_shared_try_send_connect(endpoint, &cmd, &((struct omx_cmd_send_connect __user *) uparam)->data);
		if (ret <= 0)
			return ret;
		/* fallback if ret==1 */
	}
#endif

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len + length, ETH_ZLEN));
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
	data = (char*) (connect_n + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in connect header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(connect_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(connect_n->dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(connect_n->ptype, OMX_PKT_TYPE_CONNECT);
	OMX_PKT_FIELD_FROM(connect_n->length, length);
	OMX_PKT_FIELD_FROM(connect_n->lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(connect_n->src_dst_peer_index, cmd.peer_index);

	omx_send_dprintk(eh, "CONNECT length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(data, &((struct omx_cmd_send_connect __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

	omx_queue_xmit(iface, skb, CONNECT);

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
	if (unlikely(length > OMX_TINY_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as a tiny (tried %d)\n",
		       OMX_TINY_MAX, length);
		ret = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_send_tiny(endpoint, &cmd, &((struct omx_cmd_send_tiny __user *) uparam)->data);
#endif

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
	ret = omx_set_target_peer(ph, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in tiny header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(tiny_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(tiny_n->dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(tiny_n->ptype, OMX_PKT_TYPE_TINY);
	OMX_PKT_FIELD_FROM(tiny_n->length, length);
	OMX_PKT_FIELD_FROM(tiny_n->lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(tiny_n->lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(tiny_n->session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(tiny_n, cmd.match_info);

	omx_send_dprintk(eh, "TINY length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(data, &((struct omx_cmd_send_tiny __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

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

	length = cmd.length;
	if (unlikely(length > OMX_SMALL_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as a small (tried %d)\n",
		       OMX_SMALL_MAX, length);
		ret = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_send_small(endpoint, &cmd);
#endif

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
	ret = omx_set_target_peer(ph, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in small header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(small_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(small_n->dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(small_n->ptype, OMX_PKT_TYPE_SMALL);
	OMX_PKT_FIELD_FROM(small_n->length, length);
	OMX_PKT_FIELD_FROM(small_n->lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(small_n->lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(small_n->session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(small_n, cmd.match_info);

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
omx_ioctl_send_medium(struct omx_endpoint * endpoint,
		      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_medium_frag *medium_n;
	struct omx_cmd_send_medium cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	uint32_t sendq_offset;
	struct page * page;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_medium_frag);
	int ret;
	uint32_t frag_length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send medium cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	frag_length = cmd.frag_length;
	if (unlikely(frag_length > OMX_PACKET_RING_ENTRY_SIZE)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %ld as a medium (tried %ld)\n",
		       OMX_PACKET_RING_ENTRY_SIZE, (unsigned long) frag_length);
		ret = -EINVAL;
		goto out;
	}

	sendq_offset = cmd.sendq_offset;
	if (unlikely(sendq_offset >= OMX_SENDQ_SIZE)) {
		printk(KERN_ERR "Open-MX: Cannot send medium fragment from sendq offset %ld (max %ld)\n",
		       (unsigned long) sendq_offset, (unsigned long) OMX_SENDQ_SIZE);
		ret = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_send_medium(endpoint, &cmd);
#endif

	if (unlikely(frag_length > omx_skb_copy_max
		     && hdr_len + frag_length >= ETH_ZLEN
		     && omx_skb_frags >= (frag_length >> OMX_PACKET_RING_ENTRY_SHIFT))) {
		/* use skb with frags */

		struct omx_deferred_event * defevent;
		unsigned int current_sendq_offset, remaining, desc;

		skb = omx_new_skb(/* only allocate space for the header now, we'll attach pages later */
				   hdr_len);
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			printk(KERN_INFO "Open-MX: Failed to create medium skb\n");
			ret = -ENOMEM;
			goto out;
		}

		defevent = kmalloc(sizeof(*defevent), GFP_KERNEL);
		if (unlikely(!defevent)) {
			omx_counter_inc(iface, SEND_NOMEM_MEDIUM_DEFEVENT);
			printk(KERN_INFO "Open-MX: Failed to allocate event\n");
			ret = -ENOMEM;
			goto out_with_skb;
		}

		/* locate headers */
		mh = omx_skb_mac_header(skb);
		ph = &mh->head;
		eh = &ph->eth;
		medium_n = (struct omx_pkt_medium_frag *) (ph + 1);

		/* set destination peer */
		ret = omx_set_target_peer(ph, cmd.peer_index);
		if (ret < 0) {
			printk(KERN_INFO "Open-MX: Failed to fill target peer in medium header\n");
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
		defevent->evt.sendq_offset = cmd.sendq_offset;
		omx_set_skb_destructor(skb, omx_medium_frag_skb_destructor, defevent);

	} else {
		/* use a linear skb */
		struct omx_evt_send_medium_frag_done evt;
		void *data;

		omx_counter_inc(iface, MEDIUM_FRAG_SEND_LINEAR);

		skb = omx_new_skb(/* pad to ETH_ZLEN */
				  max_t(unsigned long, hdr_len + frag_length, ETH_ZLEN));
		if (unlikely(skb == NULL)) {
			omx_counter_inc(iface, SEND_NOMEM_SKB);
			printk(KERN_INFO "Open-MX: Failed to create linear medium skb\n");
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
		ret = omx_set_target_peer(ph, cmd.peer_index);
		if (ret < 0) {
			printk(KERN_INFO "Open-MX: Failed to fill target peer in medium header\n");
			goto out_with_skb;
		}

		/* copy the data in the linear skb */
		memcpy(data, endpoint->sendq + sendq_offset, frag_length);

		/* notify the event right now */
		evt.sendq_offset = cmd.sendq_offset;
		omx_notify_exp_event(endpoint,
				     OMX_EVT_SEND_MEDIUM_FRAG_DONE,
				     &evt, sizeof(evt));
	}

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* fill omx header */
	OMX_PKT_FIELD_FROM(medium_n->msg.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(medium_n->msg.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(medium_n->msg.ptype, OMX_PKT_TYPE_MEDIUM);
	OMX_PKT_FIELD_FROM(medium_n->msg.length, cmd.msg_length);
	OMX_PKT_FIELD_FROM(medium_n->msg.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(medium_n->msg.lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(medium_n->msg.session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(&medium_n->msg, cmd.match_info);
	OMX_PKT_FIELD_FROM(medium_n->frag_length, frag_length);
	OMX_PKT_FIELD_FROM(medium_n->frag_seqnum, cmd.frag_seqnum);
	OMX_PKT_FIELD_FROM(medium_n->frag_pipeline, cmd.frag_pipeline);

	omx_send_dprintk(eh, "MEDIUM FRAG length %ld", (unsigned long) frag_length);

	omx_queue_xmit(iface, skb, MEDIUM_FRAG);

	return 0;

 out_with_skb:
	kfree_skb(skb);
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
	struct omx_pkt_msg *rndv_n;
	struct omx_cmd_send_rndv_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_msg);
	char * data;
	int ret;
	uint8_t length;

	ret = copy_from_user(&cmd, &((struct omx_cmd_send_rndv __user *) uparam)->hdr, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send rndv cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (unlikely(length > OMX_RNDV_DATA_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as a rndv (tried %d)\n",
		       OMX_RNDV_DATA_MAX, length);
		ret = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_send_rndv(endpoint, &cmd, &((struct omx_cmd_send_rndv __user *) uparam)->data);
#endif

	if (omx_region_demand_pin) {
		/* make sure the region is pinned */
		struct omx_user_region * region;
		struct omx_user_region_pin_state pinstate;

		region = omx_user_region_acquire(endpoint, cmd.user_region_id_needed);
		if (unlikely(!region)) {
			ret = -EINVAL;
			goto out;
		}

		omx_user_region_demand_pin_init(&pinstate, region);
		pinstate.next_chunk_pages = omx_pin_chunk_pages_max;
		ret = omx_user_region_demand_pin_finish(&pinstate); /* will be _or_parallel once we overlap here too */
		omx_user_region_release(region);
		if (ret < 0) {
			dprintk(REG, "failed to pin user region\n");
			goto out;
		}
	}

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len + length, ETH_ZLEN));
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
	rndv_n = (struct omx_pkt_msg *) (ph + 1);
	data = (char*) (rndv_n + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in rndv header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(rndv_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(rndv_n->dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(rndv_n->ptype, OMX_PKT_TYPE_RNDV);
	OMX_PKT_FIELD_FROM(rndv_n->length, length);
	OMX_PKT_FIELD_FROM(rndv_n->lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(rndv_n->lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(rndv_n->session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(rndv_n, cmd.match_info);

	omx_send_dprintk(eh, "RNDV length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(data, &((struct omx_cmd_send_rndv __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send rndv cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

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

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_send_notify(endpoint, &cmd);
#endif

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
	ret = omx_set_target_peer(ph, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(notify_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(notify_n->dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(notify_n->ptype, OMX_PKT_TYPE_NOTIFY);
	OMX_PKT_FIELD_FROM(notify_n->total_length, cmd.total_length);
	OMX_PKT_FIELD_FROM(notify_n->lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(notify_n->lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(notify_n->session, cmd.session_id);
	OMX_PKT_FIELD_FROM(notify_n->puller_rdma_id, cmd.puller_rdma_id);
	OMX_PKT_FIELD_FROM(notify_n->puller_rdma_seqnum, cmd.puller_rdma_seqnum);

	omx_send_dprintk(eh, "NOTIFY");

	omx_queue_xmit(iface, skb, NOTIFY);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

int
omx_ioctl_send_truc(struct omx_endpoint * endpoint,
		    void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_truc *truc_n;
	struct omx_cmd_send_truc_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_truc);
	char * data;
	uint8_t length;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send truc cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (unlikely(length > OMX_TRUC_DATA_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %d as truc data (tried %d)\n",
		       OMX_TRUC_DATA_MAX, length);
		ret = -EINVAL;
		goto out;
	}

#ifndef OMX_DISABLE_SHARED
	if (unlikely(cmd.shared))
		return omx_shared_send_truc(endpoint, &cmd, &((struct omx_cmd_send_truc __user *) uparam)->data);
#endif

	skb = omx_new_skb(/* pad to ETH_ZLEN */
			  max_t(unsigned long, hdr_len + length, ETH_ZLEN));
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
	data = (char*) (truc_n + 1);

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(ph, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in truc header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(truc_n->src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(truc_n->dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(truc_n->ptype, OMX_PKT_TYPE_TRUC);
	OMX_PKT_FIELD_FROM(truc_n->length, length);
	OMX_PKT_FIELD_FROM(truc_n->session, cmd.session_id);

	omx_send_dprintk(eh, "TRUC length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(data, &((struct omx_cmd_send_truc __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send truc cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

	omx_queue_xmit(iface, skb, TRUC);

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
	ret = omx_set_target_peer(ph, peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		/* FIXME: BUG? */
		goto out_with_skb;
	}
	nack_lib_n->dst_src_peer_index = ph->dst_src_peer_index;

	/* fill omx header */
	OMX_PKT_FIELD_FROM(nack_lib_n->src_endpoint, src_endpoint);
	OMX_PKT_FIELD_FROM(nack_lib_n->dst_endpoint, dst_endpoint);
	OMX_PKT_FIELD_FROM(nack_lib_n->ptype, OMX_PKT_TYPE_NACK_LIB);
	OMX_PKT_FIELD_FROM(nack_lib_n->nack_type, nack_type);
	OMX_PKT_FIELD_FROM(nack_lib_n->lib_seqnum, lib_seqnum);

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
	ret = omx_set_target_peer(ph, peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		/* FIXME: BUG? */
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(nack_mcp_n->src_endpoint, src_endpoint);
	OMX_PKT_FIELD_FROM(nack_mcp_n->ptype, OMX_PKT_TYPE_NACK_MCP);
	OMX_PKT_FIELD_FROM(nack_mcp_n->nack_type, nack_type);
	OMX_PKT_FIELD_FROM(nack_mcp_n->src_pull_handle, src_pull_handle);
	OMX_PKT_FIELD_FROM(nack_mcp_n->src_magic, src_magic);

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
	char data[OMX_TINY_MAX];
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

	ret = copy_from_user(data, &((struct omx_cmd_bench __user *) uparam)->dummy_data, OMX_TINY_MAX);
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

	omx_notify_exp_event(endpoint, OMX_EVT_NONE, NULL, 0);

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
	kfree_skb(skb);
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
