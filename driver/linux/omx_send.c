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
#include <linux/mm.h>

#include "omx_common.h"
#include "omx_hal.h"
#include "omx_wire_access.h"

#ifdef OMX_DEBUG
static unsigned long omx_tiny_packet_loss_index = 0;
static unsigned long omx_small_packet_loss_index = 0;
static unsigned long omx_medium_packet_loss_index = 0;
static unsigned long omx_rndv_packet_loss_index = 0;
static unsigned long omx_notify_packet_loss_index = 0;
static unsigned long omx_connect_packet_loss_index = 0;
#endif

/*************************************
 * Allocate and initialize a OMX skb
 */
struct sk_buff *
omx_new_skb(struct net_device *ifp, unsigned long len)
{
	struct sk_buff *skb;

	skb = omx_netdev_alloc_skb(ifp, len);
	if (likely(skb != NULL)) {
		omx_skb_reset_mac_header(skb);
		omx_skb_reset_network_header(skb);
		skb->protocol = __constant_htons(ETH_P_OMX);
		skb->priority = 0;
		skb_put(skb, len);
		memset(skb->head, 0, len);
		skb->next = skb->prev = NULL;

		/* tell the network layer not to perform IP checksums
		 * or to get the NIC to do it
		 */
		skb->ip_summed = CHECKSUM_NONE;

		/* skb->sk is used as a pointer to a private data, initialize it */
		skb->sk = NULL;
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
	struct omx_deferred_event * defevent = (void *) skb->sk;
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
omx_send_tiny(struct omx_endpoint * endpoint,
	      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_tiny_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
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

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr) + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create tiny skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in tiny header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.tiny.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.tiny.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.tiny.ptype, OMX_PKT_TYPE_TINY);
	OMX_PKT_FIELD_FROM(mh->body.tiny.length, length);
	OMX_PKT_FIELD_FROM(mh->body.tiny.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(mh->body.tiny.lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(mh->body.tiny.session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(&mh->body.tiny, cmd.match_info);

	omx_send_dprintk(eh, "TINY length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_tiny __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

#ifdef OMX_DEBUG
	if (++omx_tiny_packet_loss_index == omx_tiny_packet_loss) {
		kfree_skb(skb);
		omx_tiny_packet_loss_index = 0;
	} else
#endif
		dev_queue_xmit(skb);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

int
omx_send_small(struct omx_endpoint * endpoint,
	       void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_small cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
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

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr) + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create small skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in small header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.small.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.small.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.small.ptype, OMX_PKT_TYPE_SMALL);
	OMX_PKT_FIELD_FROM(mh->body.small.length, length);
	OMX_PKT_FIELD_FROM(mh->body.small.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(mh->body.small.lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(mh->body.small.session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(& mh->body.small, cmd.match_info);

	omx_send_dprintk(eh, "SMALL length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, (void *)(unsigned long) cmd.vaddr, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send small cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

#ifdef OMX_DEBUG
	if (++omx_small_packet_loss_index == omx_small_packet_loss) {
		kfree_skb(skb);
		omx_small_packet_loss_index = 0;
	} else
#endif
		dev_queue_xmit(skb);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

int
omx_send_medium(struct omx_endpoint * endpoint,
		void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_medium cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	uint16_t sendq_page_offset;
	struct page * page;
	struct omx_deferred_event * defevent;
	int ret;
	uint32_t frag_length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send medium cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	frag_length = cmd.frag_length;
	if (unlikely(frag_length > OMX_SENDQ_ENTRY_SIZE)) {
		printk(KERN_ERR "Open-MX: Cannot send more than %ld as a medium (tried %ld)\n",
		       PAGE_SIZE * 1UL, (unsigned long) frag_length);
		ret = -EINVAL;
		goto out;
	}

	sendq_page_offset = cmd.sendq_page_offset;
	if (unlikely(sendq_page_offset >= OMX_SENDQ_ENTRY_NR)) {
		printk(KERN_ERR "Open-MX: Cannot send medium fragment from sendq page offset %ld (max %ld)\n",
		       (unsigned long) sendq_page_offset, (unsigned long) OMX_SENDQ_ENTRY_NR);
		ret = -EINVAL;
		goto out;
	}

	defevent = kmalloc(sizeof(*defevent), GFP_KERNEL);
	if (unlikely(!defevent)) {
		printk(KERN_INFO "Open-MX: Failed to allocate event\n");
		ret = -ENOMEM;
		goto out;
	}

	skb = omx_new_skb(ifp,
			  /* only allocate space for the header now,
			   * we'll attach pages and pad to ETH_ZLEN later
			   */
			   sizeof(*mh));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create medium skb\n");
		ret = -ENOMEM;
		goto out_with_event;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in medium header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.ptype, OMX_PKT_TYPE_MEDIUM);
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.length, cmd.msg_length);
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(mh->body.medium.msg.session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(& mh->body.medium.msg, cmd.match_info);
	OMX_PKT_FIELD_FROM(mh->body.medium.frag_length, frag_length);
	OMX_PKT_FIELD_FROM(mh->body.medium.frag_seqnum, cmd.frag_seqnum);
	OMX_PKT_FIELD_FROM(mh->body.medium.frag_pipeline, cmd.frag_pipeline);

	omx_send_dprintk(eh, "MEDIUM FRAG length %ld", (unsigned long) frag_length);

	/* attach the sendq page */
	page = endpoint->sendq_pages[sendq_page_offset];
	get_page(page);
	skb_fill_page_desc(skb, 0, page, 0, frag_length);
	skb->len += frag_length;
	skb->data_len = frag_length;

 	if (unlikely(skb->len < ETH_ZLEN)) {
		/* pad to ETH_ZLEN */
		ret = omx_skb_pad(skb, ETH_ZLEN);
		if (ret)
			/* skb has been freed in skb_pad */
			goto out_with_event;
		skb->len = ETH_ZLEN;
	}

	/* prepare the deferred event now that we cannot fail anymore */
	defevent->endpoint = endpoint;
	defevent->evt.sendq_page_offset = cmd.sendq_page_offset;
	skb->sk = (void *) defevent;
	skb->destructor = omx_medium_frag_skb_destructor;

#ifdef OMX_DEBUG
	if (++omx_medium_packet_loss_index == omx_medium_packet_loss) {
		kfree_skb(skb);
		omx_medium_packet_loss_index = 0;
	} else
#endif
		dev_queue_xmit(skb);

	/* return>0 to tell the caller to not release the endpoint,
	 * we will do it when releasing the skb in the destructor
	 */
	return 1;

 out_with_skb:
	dev_kfree_skb(skb);
 out_with_event:
	kfree(defevent);
 out:
	return ret;
}

int
omx_send_rndv(struct omx_endpoint * endpoint,
	      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_rndv_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
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

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr) + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create rndv skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in rndv header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.rndv.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.rndv.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.rndv.ptype, OMX_PKT_TYPE_RNDV);
	OMX_PKT_FIELD_FROM(mh->body.rndv.length, length);
	OMX_PKT_FIELD_FROM(mh->body.rndv.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(mh->body.rndv.lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(mh->body.rndv.session, cmd.session_id);
	OMX_PKT_MATCH_INFO_FROM(& mh->body.rndv, cmd.match_info);

	omx_send_dprintk(eh, "RNDV length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_rndv __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send rndv cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

#ifdef OMX_DEBUG
	if (++omx_rndv_packet_loss_index == omx_rndv_packet_loss) {
		kfree_skb(skb);
		omx_rndv_packet_loss_index = 0;
	} else
#endif
		dev_queue_xmit(skb);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

int
omx_send_connect(struct omx_endpoint * endpoint,
		 void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_connect_hdr cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
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

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr) + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create connect skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in connect header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.connect.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.connect.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.connect.ptype, OMX_PKT_TYPE_CONNECT);
	OMX_PKT_FIELD_FROM(mh->body.connect.length, length);
	OMX_PKT_FIELD_FROM(mh->body.connect.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(mh->body.connect.src_dst_peer_index, cmd.peer_index);
	OMX_PKT_FIELD_FROM(mh->body.connect.src_mac_low32, (uint32_t) omx_board_addr_from_netdevice(ifp));

	omx_send_dprintk(eh, "CONNECT length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_connect __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

#ifdef OMX_DEBUG
	if (++omx_connect_packet_loss_index == omx_connect_packet_loss) {
		kfree_skb(skb);
		omx_connect_packet_loss_index = 0;
	} else
#endif
		dev_queue_xmit(skb);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

int
omx_send_notify(struct omx_endpoint * endpoint,
		void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_notify cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send notify cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr), ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create notify skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.notify.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.notify.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.notify.ptype, OMX_PKT_TYPE_NOTIFY);
	OMX_PKT_FIELD_FROM(mh->body.notify.total_length, cmd.total_length);
	OMX_PKT_FIELD_FROM(mh->body.notify.lib_seqnum, cmd.seqnum);
	OMX_PKT_FIELD_FROM(mh->body.notify.lib_piggyack, cmd.piggyack);
	OMX_PKT_FIELD_FROM(mh->body.notify.session, cmd.session_id);
	OMX_PKT_FIELD_FROM(mh->body.notify.puller_rdma_id, cmd.puller_rdma_id);
	OMX_PKT_FIELD_FROM(mh->body.notify.puller_rdma_seqnum, cmd.puller_rdma_seqnum);

	omx_send_dprintk(eh, "NOTIFY");

#ifdef OMX_DEBUG
	if (++omx_notify_packet_loss_index == omx_notify_packet_loss) {
		kfree_skb(skb);
		omx_notify_packet_loss_index = 0;
	} else
#endif
		dev_queue_xmit(skb);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

int
omx_send_truc(struct omx_endpoint * endpoint,
	      void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct omx_cmd_send_truc cmd;
	struct omx_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
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

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr) + length, ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create truc skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, cmd.peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in truc header\n");
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.truc.src_endpoint, endpoint->endpoint_index);
	OMX_PKT_FIELD_FROM(mh->body.truc.dst_endpoint, cmd.dest_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.truc.ptype, OMX_PKT_TYPE_TRUC);
	OMX_PKT_FIELD_FROM(mh->body.truc.length, length);
	OMX_PKT_FIELD_FROM(mh->body.truc.session, cmd.session_id);

	omx_send_dprintk(eh, "TRUC length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_truc __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send truc cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

	dev_queue_xmit(skb);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

void
omx_send_nack_lib(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type,
		  uint8_t src_endpoint, uint8_t dst_endpoint, uint16_t lib_seqnum)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct ethhdr *eh;
	struct net_device * ifp = iface->eth_ifp;
	int ret;

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr), ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create nack lib skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		/* FIXME: BUG? */
		goto out_with_skb;
	}
	mh->body.nack_lib.dst_src_peer_index = mh->head.dst_src_peer_index;

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.nack_lib.src_endpoint, src_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.nack_lib.dst_endpoint, dst_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.nack_lib.ptype, OMX_PKT_TYPE_NACK_LIB);
	OMX_PKT_FIELD_FROM(mh->body.nack_lib.nack_type, nack_type);
	OMX_PKT_FIELD_FROM(mh->body.nack_lib.lib_seqnum, lib_seqnum);

	omx_send_dprintk(eh, "NACK LIB type %d", nack_type);

	dev_queue_xmit(skb);

	return;

 out_with_skb:
	dev_kfree_skb(skb);
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
	struct ethhdr *eh;
	struct net_device * ifp = iface->eth_ifp;
	int ret;

	skb = omx_new_skb(ifp,
			  /* pad to ETH_ZLEN */
			  max_t(unsigned long, sizeof(struct omx_hdr), ETH_ZLEN));
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create nack mcp skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = omx_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));

	/* set destination peer */
	ret = omx_set_target_peer(mh, peer_index);
	if (ret < 0) {
		printk(KERN_INFO "Open-MX: Failed to fill target peer in notify header\n");
		/* FIXME: BUG? */
		goto out_with_skb;
	}

	/* fill omx header */
	OMX_PKT_FIELD_FROM(mh->body.nack_mcp.src_endpoint, src_endpoint);
	OMX_PKT_FIELD_FROM(mh->body.nack_mcp.ptype, OMX_PKT_TYPE_NACK_MCP);
	OMX_PKT_FIELD_FROM(mh->body.nack_mcp.nack_type, nack_type);
	OMX_PKT_FIELD_FROM(mh->body.nack_mcp.src_pull_handle, src_pull_handle);
	OMX_PKT_FIELD_FROM(mh->body.nack_mcp.src_magic, src_magic);

	omx_send_dprintk(eh, "NACK MCP type %d", nack_type);

	dev_queue_xmit(skb);

	return;

 out_with_skb:
	dev_kfree_skb(skb);
 out:
	/* just forget about it, it will be resent anyway */
	/* return ret; */
	return;
}

/*
 * Command to benchmark commands
 */
int
omx_cmd_bench(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct sk_buff *skb;
	struct omx_hdr *mh;
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

	skb = omx_new_skb(ifp, ETH_ZLEN);
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create bench skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* level 02: alloc skb */
	if (cmd.type == OMX_CMD_BENCH_TYPE_SEND_ALLOC)
		goto out_with_skb;

	mh = omx_hdr(skb);
	eh = &mh->head.eth;
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

	dev_queue_xmit(skb);

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
