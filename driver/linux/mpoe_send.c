#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

/*************************************
 * Allocate and initialize a MPOE skb
 */
struct sk_buff *
mpoe_new_skb(struct net_device *ifp, unsigned long len)
{
	struct sk_buff *skb;

	skb = mpoe_netdev_alloc_skb(ifp, len);
	if (skb) {
		mpoe_skb_reset_mac_header(skb);
		mpoe_skb_reset_network_header(skb);
		skb->protocol = __constant_htons(ETH_P_MPOE);
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

struct mpoe_deferred_event {
	struct mpoe_endpoint *endpoint;
	union mpoe_evt evt;
};

/* medium frag skb destructor to release sendq pages */
static void
mpoe_medium_frag_skb_destructor(struct sk_buff *skb)
{
	struct mpoe_deferred_event * defevent = (void *) skb->sk;
	struct mpoe_endpoint * endpoint = defevent->endpoint;
	union mpoe_evt * evt;

	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Failed to complete send of MEDIUM packet because of event queue full\n");
		/* FIXME: the application sucks, it should take care of events sooner, queue it? */
		return;
	}

	/* report the event to user-space (fortunately memcpy will write the ending type after everything
	 * else so that the application detects the event once it is fully copied)
	 */
	memcpy(&evt->send_medium_frag_done, &defevent->evt, sizeof(struct mpoe_evt_send_medium_frag_done));

	/* release objects now */
	mpoe_endpoint_release(endpoint);
	kfree(defevent);
}

/*********************
 * Main send routines
 */

int
mpoe_send_tiny(struct mpoe_endpoint * endpoint,
	       void __user * uparam)
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_cmd_send_tiny_hdr cmd;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	int ret;
	uint8_t length;

	ret = copy_from_user(&cmd, &((struct mpoe_cmd_send_tiny __user *) uparam)->hdr, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send tiny cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (length > MPOE_TINY_MAX) {
		printk(KERN_ERR "MPoE: Cannot send more than %d as a tiny (tried %d)\n",
		       MPOE_TINY_MAX, length);
		ret = -EINVAL;
		goto out;
	}

	skb = mpoe_new_skb(ifp,
			   /* pad to ETH_ZLEN */
			   max_t(unsigned long, sizeof(struct mpoe_hdr) + length, ETH_ZLEN));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create tiny skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = mpoe_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	mpoe_mac_addr_to_ethhdr_dst(&cmd.dest_addr, eh);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

#ifdef MPOE_DEBUG
	printk("MPoE: sending TINY %d from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       length,
	       eh->h_source[0], eh->h_source[1], eh->h_source[2],
	       eh->h_source[3], eh->h_source[4], eh->h_source[5],
	       eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],
	       eh->h_dest[3], eh->h_dest[4], eh->h_dest[5]);
#endif
	/* fill mpoe header */
	mh->body.tiny.src_endpoint = endpoint->endpoint_index;
	mh->body.tiny.dst_endpoint = cmd.dest_endpoint;
	mh->body.tiny.ptype = MPOE_PKT_TYPE_TINY;
	mh->body.tiny.length = length;
	mh->body.tiny.lib_seqnum = cmd.seqnum;
	MPOE_PKT_FROM_MATCH_INFO(& mh->body.tiny, cmd.match_info);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct mpoe_cmd_send_tiny __user *) uparam)->data, length);
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send tiny cmd data\n");
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
mpoe_send_small(struct mpoe_endpoint * endpoint,
		void __user * uparam)
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_cmd_send_small cmd;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	int ret;
	uint32_t length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send small cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (length > MPOE_SMALL_MAX) {
		printk(KERN_ERR "MPoE: Cannot send more than %d as a small (tried %d)\n",
		       MPOE_SMALL_MAX, length);
		ret = -EINVAL;
		goto out;
	}

	skb = mpoe_new_skb(ifp,
			   /* pad to ETH_ZLEN */
			   max_t(unsigned long, sizeof(struct mpoe_hdr) + length, ETH_ZLEN));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create small skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = mpoe_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	mpoe_mac_addr_to_ethhdr_dst(&cmd.dest_addr, eh);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

#ifdef MPOE_DEBUG
	printk("MPoE: sending SMALL %d from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       length,
	       eh->h_source[0], eh->h_source[1], eh->h_source[2],
	       eh->h_source[3], eh->h_source[4], eh->h_source[5],
	       eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],
	       eh->h_dest[3], eh->h_dest[4], eh->h_dest[5]);
#endif

	/* fill mpoe header */
	mh->body.small.src_endpoint = endpoint->endpoint_index;
	mh->body.small.dst_endpoint = cmd.dest_endpoint;
	mh->body.small.ptype = MPOE_PKT_TYPE_SMALL;
	mh->body.small.length = length;
	mh->body.small.lib_seqnum = cmd.seqnum;
	MPOE_PKT_FROM_MATCH_INFO(& mh->body.small, cmd.match_info);

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, (void *)(unsigned long) cmd.vaddr, length);
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send small cmd data\n");
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
mpoe_send_medium(struct mpoe_endpoint * endpoint,
		 void __user * uparam)
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_cmd_send_medium cmd;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct page * page;
	struct mpoe_deferred_event * event;
	int ret;
	uint32_t frag_length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send medium cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	frag_length = cmd.frag_length;
	if (frag_length > MPOE_SENDQ_ENTRY_SIZE) {
		printk(KERN_ERR "MPoE: Cannot send more than %ld as a medium (tried %ld)\n",
		       PAGE_SIZE * 1UL, (unsigned long) frag_length);
		ret = -EINVAL;
		goto out;
	}

	event = kmalloc(sizeof(*event), GFP_KERNEL);
	if (!event) {
		printk(KERN_INFO "MPoE: Failed to allocate event\n");
		ret = -ENOMEM;
		goto out;
	}

	skb = mpoe_new_skb(ifp,
			   /* only allocate space for the header now,
			    * we'll attach pages and pad to ETH_ZLEN later
			    */
			   sizeof(*mh));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create medium skb\n");
		ret = -ENOMEM;
		goto out_with_event;
	}

	/* locate headers */
	mh = mpoe_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	mpoe_mac_addr_to_ethhdr_dst(&cmd.dest_addr, eh);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

#ifdef MPOE_DEBUG
	printk("MPoE: sending MEDIUM_FRAG %d from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frag_length,
	       eh->h_source[0], eh->h_source[1], eh->h_source[2],
	       eh->h_source[3], eh->h_source[4], eh->h_source[5],
	       eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],
	       eh->h_dest[3], eh->h_dest[4], eh->h_dest[5]);
#endif
	/* fill mpoe header */
	mh->body.medium.msg.src_endpoint = endpoint->endpoint_index;
	mh->body.medium.msg.dst_endpoint = cmd.dest_endpoint;
	mh->body.medium.msg.ptype = MPOE_PKT_TYPE_MEDIUM;
	mh->body.medium.msg.length = cmd.msg_length;
	mh->body.medium.msg.lib_seqnum = cmd.seqnum;
	MPOE_PKT_FROM_MATCH_INFO(& mh->body.medium.msg, cmd.match_info);
	mh->body.medium.frag_length = frag_length;
	mh->body.medium.frag_seqnum = cmd.frag_seqnum;
	mh->body.medium.frag_pipeline = cmd.frag_pipeline;

	/* attach the sendq page */
	page = vmalloc_to_page(endpoint->sendq + (cmd.sendq_page_offset << PAGE_SHIFT));
	BUG_ON(page == NULL);
	get_page(page);
	skb_fill_page_desc(skb, 0, page, 0, frag_length);
	skb->len += frag_length;
	skb->data_len = frag_length;

 	if (unlikely(skb->len < ETH_ZLEN)) {
		/* pad to ETH_ZLEN */
		ret = skb_pad(skb, ETH_ZLEN);
		if (ret < 0)
			/* skb has been freed in skb_pad */
			goto out_with_event;
		skb->len = ETH_ZLEN;
	}

	/* prepare the deferred event now that we cannot fail anymore */
	event->endpoint = endpoint;
	event->evt.send_medium_frag_done.sendq_page_offset = cmd.sendq_page_offset;
	event->evt.generic.type = MPOE_EVT_SEND_MEDIUM_FRAG_DONE;
	skb->sk = (void *) event;
	skb->destructor = mpoe_medium_frag_skb_destructor;

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
mpoe_send_rendez_vous(struct mpoe_endpoint * endpoint,
		      void __user * uparam)
{
	return -ENOSYS;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
