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
	union omx_evt evt;
};

/* medium frag skb destructor to release sendq pages */
static void
omx_medium_frag_skb_destructor(struct sk_buff *skb)
{
	struct omx_deferred_event * defevent = (void *) skb->sk;
	struct omx_endpoint * endpoint = defevent->endpoint;
	union omx_evt * evt;

	/* get the eventq slot */
	evt = omx_find_next_exp_eventq_slot(endpoint);
	if (unlikely(!evt) ){
		/* the application sucks, it did not check the expected eventq before posting requests */
		printk(KERN_INFO "Open-MX: Failed to complete send of MEDIUM packet because of expected event queue full\n");
		return;
	}

	/* report the event to user-space (fortunately memcpy will write the ending type after everything
	 * else so that the application detects the event once it is fully copied)
	 */
	memcpy(&evt->send_medium_frag_done, &defevent->evt, sizeof(struct omx_evt_send_medium_frag_done));

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
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	mh->head.dst_src_peer_index = cmd.dest_src_peer_index;
	mh->body.tiny.src_endpoint = endpoint->endpoint_index;
	mh->body.tiny.dst_endpoint = cmd.dest_endpoint;
	mh->body.tiny.ptype = OMX_PKT_TYPE_TINY;
	mh->body.tiny.length = length;
	mh->body.tiny.lib_seqnum = cmd.seqnum;
	mh->body.tiny.session = cmd.session_id;
	OMX_PKT_FROM_MATCH_INFO(& mh->body.tiny, cmd.match_info);

	omx_send_dprintk(eh, "TINY length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_tiny __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd data\n");
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
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	mh->head.dst_src_peer_index = cmd.dest_src_peer_index;
	mh->body.small.src_endpoint = endpoint->endpoint_index;
	mh->body.small.dst_endpoint = cmd.dest_endpoint;
	mh->body.small.ptype = OMX_PKT_TYPE_SMALL;
	mh->body.small.length = length;
	mh->body.small.lib_seqnum = cmd.seqnum;
	mh->body.small.session = cmd.session_id;
	OMX_PKT_FROM_MATCH_INFO(& mh->body.small, cmd.match_info);

	omx_send_dprintk(eh, "SMALL length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, (void *)(unsigned long) cmd.vaddr, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send small cmd data\n");
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
	struct omx_deferred_event * event;
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

	event = kmalloc(sizeof(*event), GFP_KERNEL);
	if (unlikely(!event)) {
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
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	mh->head.dst_src_peer_index = cmd.dest_src_peer_index;
	mh->body.medium.msg.src_endpoint = endpoint->endpoint_index;
	mh->body.medium.msg.dst_endpoint = cmd.dest_endpoint;
	mh->body.medium.msg.ptype = OMX_PKT_TYPE_MEDIUM;
	mh->body.medium.msg.length = cmd.msg_length;
	mh->body.medium.msg.lib_seqnum = cmd.seqnum;
	mh->body.medium.msg.session = cmd.session_id;
	OMX_PKT_FROM_MATCH_INFO(& mh->body.medium.msg, cmd.match_info);
	mh->body.medium.frag_length = frag_length;
	mh->body.medium.frag_seqnum = cmd.frag_seqnum;
	mh->body.medium.frag_pipeline = cmd.frag_pipeline;

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
	event->endpoint = endpoint;
	event->evt.send_medium_frag_done.sendq_page_offset = cmd.sendq_page_offset;
	/* no need to enforce the type at the end since we don't write to user-space yet */
	event->evt.generic.type = OMX_EVT_SEND_MEDIUM_FRAG_DONE;
	skb->sk = (void *) event;
	skb->destructor = omx_medium_frag_skb_destructor;

	dev_queue_xmit(skb);

	/* return>0 to tell the caller to not release the endpoint,
	 * we will do it when releasing the skb in the destructor
	 */
	return 1;

 out_with_event:
	kfree(event);
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
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	mh->head.dst_src_peer_index = cmd.dest_src_peer_index;
	mh->body.rndv.src_endpoint = endpoint->endpoint_index;
	mh->body.rndv.dst_endpoint = cmd.dest_endpoint;
	mh->body.rndv.ptype = OMX_PKT_TYPE_RNDV;
	mh->body.rndv.length = length;
	mh->body.rndv.lib_seqnum = cmd.seqnum;
	mh->body.rndv.session = cmd.session_id;
	OMX_PKT_FROM_MATCH_INFO(& mh->body.rndv, cmd.match_info);

	omx_send_dprintk(eh, "RNDV length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_rndv __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send rndv cmd data\n");
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
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	mh->body.connect.src_endpoint = endpoint->endpoint_index;
	mh->body.connect.dst_endpoint = cmd.dest_endpoint;
	mh->body.connect.ptype = OMX_PKT_TYPE_CONNECT;
	mh->body.connect.length = length;
	mh->body.connect.lib_seqnum = cmd.seqnum;
	mh->body.connect.src_dst_peer_index = cmd.src_dest_peer_index;
	mh->body.connect.src_mac_low32 = (uint32_t) omx_board_addr_from_netdevice(ifp);

	omx_send_dprintk(eh, "CONNECT length %ld", (unsigned long) length);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct omx_cmd_send_connect __user *) uparam)->data, length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send connect cmd data\n");
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
	memset(eh, 0, sizeof(*eh));
	omx_board_addr_to_ethhdr_dst(eh, cmd.dest_addr);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);

	/* fill omx header */
	mh->head.dst_src_peer_index = cmd.dest_src_peer_index;
	mh->body.notify.src_endpoint = endpoint->endpoint_index;
	mh->body.notify.dst_endpoint = cmd.dest_endpoint;
	mh->body.notify.ptype = OMX_PKT_TYPE_NOTIFY;
	mh->body.notify.total_length = cmd.total_length;
	mh->body.notify.lib_seqnum = cmd.seqnum;
	mh->body.notify.session = cmd.session_id;
	mh->body.notify.puller_rdma_id = cmd.puller_rdma_id;
	mh->body.notify.puller_rdma_seqnum = cmd.puller_rdma_seqnum;

	omx_send_dprintk(eh, "NOTIFY");

	dev_queue_xmit(skb);

	return 0;

 out:
	return ret;
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
	union omx_evt *evt;
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
	BUG_ON(!endpoint);

	/* level 11: recv acquire */
	if (cmd.type == OMX_CMD_BENCH_TYPE_RECV_ACQU)
		goto out_with_endpoint;

	evt = omx_find_next_exp_eventq_slot(endpoint);
	if (unlikely(!evt)) {
		dprintk("BENCH command failed of expected event queue full");
		ret = -EBUSY;
		goto out_with_endpoint;
	}

	/* level 12: recv alloc */
	if (cmd.type == OMX_CMD_BENCH_TYPE_RECV_ALLOC)
		goto out_with_endpoint;

	memcpy(evt->generic.pad, data, OMX_TINY_MAX);
	evt->generic.type = OMX_EVT_NONE;
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
