#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

/*
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

struct mpoe_deferred_event {
	struct mpoe_endpoint *endpoint;
	union mpoe_evt evt;
};

static void
mpoe_medium_frag_skb_destructor(struct sk_buff *skb)
{
	struct mpoe_deferred_event * defevent = (void *) skb->sk;
	struct mpoe_endpoint * endpoint = defevent->endpoint;
	union mpoe_evt * evt;

	/* FIXME: need to acquire the endpoint */

	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Failed to complete send of MEDIUM packet because of event queue full\n");
		/* FIXME: the application sucks, it should take care of events sooner, queue it? */
		return;
	}
	printk("destructor called\n");

	memcpy(&evt->send_done, &defevent->evt, sizeof(struct mpoe_evt_send_done));

	/* release objects now */
	mpoe_endpoint_release(endpoint);
	kfree(defevent);
}

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
	union mpoe_evt * evt;
	struct mpoe_evt_send_done * event;
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

	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Failed to send TINY packet because of event queue full\n");
		ret = -EBUSY;
		goto out;
	}
	event = &evt->send_done;

	skb = mpoe_new_skb(ifp,
			   sizeof(struct mpoe_hdr) + length);
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create tiny skb\n");
		ret = -ENOMEM;
		/* FIXME: restore the event in the queue */
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

	/* fill mpoe header */
	mh->body.tiny.src_endpoint = endpoint->endpoint_index;
	mh->body.tiny.dst_endpoint = cmd.dest_endpoint;
	mh->body.tiny.ptype = MPOE_PKT_TINY;
	mh->body.tiny.length = length;
	mh->body.tiny.match_a = cmd.match_info >> 32;
	mh->body.tiny.match_b = cmd.match_info & 0xffffffff;

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct mpoe_cmd_send_tiny __user *) uparam)->data, length);
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		/* FIXME: restore the event in the queue */
		goto out_with_skb;
	}

	dev_queue_xmit(skb);

	/* return the event */
	event->lib_cookie = cmd.lib_cookie;
	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_SEND_DONE;

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
	union mpoe_evt * evt;
	struct mpoe_evt_send_done * event;
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

	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Failed to send SMALL packet because of event queue full\n");
		ret = -EBUSY;
		goto out;
	}
	event = &evt->send_done;

	skb = mpoe_new_skb(ifp,
			   sizeof(struct mpoe_hdr) + length);
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create small skb\n");
		ret = -ENOMEM;
		/* FIXME: restore the event in the queue */
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

	/* fill mpoe header */
	mh->body.small.src_endpoint = endpoint->endpoint_index;
	mh->body.small.dst_endpoint = cmd.dest_endpoint;
	mh->body.small.ptype = MPOE_PKT_SMALL;
	mh->body.small.length = length;
	mh->body.small.match_a = cmd.match_info >> 32;
	mh->body.small.match_b = cmd.match_info & 0xffffffff;

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, (void *)(unsigned long) cmd.vaddr, length);
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send small cmd data\n");
		ret = -EFAULT;
		/* FIXME: restore the event in the queue */
		goto out_with_skb;
	}

	dev_queue_xmit(skb);

	/* return the event */
	event->lib_cookie = cmd.lib_cookie;
	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_SEND_DONE;

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
	uint32_t length;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send medium cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd.length;
	if (length > PAGE_SIZE) { /* FIXME */
		printk(KERN_ERR "MPoE: Cannot send more than %ld as a medium (tried %ld)\n",
		       PAGE_SIZE * 1UL, (unsigned long) length);
		ret = -EINVAL;
		goto out;
	}

	event = kmalloc(sizeof(*event), GFP_KERNEL);
	if (!event) {
		printk(KERN_INFO "MPoE: Failed to allocate event\n");
		ret = -ENOMEM;
		goto out;
	}

	skb = mpoe_new_skb(ifp, sizeof(*mh));
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

	/* fill mpoe header */
	mh->body.medium.msg.src_endpoint = endpoint->endpoint_index;
	mh->body.medium.msg.dst_endpoint = cmd.dest_endpoint;
	mh->body.medium.msg.ptype = MPOE_PKT_MEDIUM;
	mh->body.medium.msg.match_a = cmd.match_info >> 32;
	mh->body.medium.msg.match_b = cmd.match_info & 0xffffffff;
	mh->body.medium.msg.length = cmd.msg_length;
	mh->body.medium.length = length;
	mh->body.medium.seqnum = cmd.seqnum;
	mh->body.medium.pipeline = cmd.pipeline;

	/* attach the sendq page */
	page = vmalloc_to_page(endpoint->sendq + (cmd.sendq_page_offset << PAGE_SHIFT));
	BUG_ON(page == NULL);
	get_page(page);
	skb_fill_page_desc(skb, 0, page, 0, length);
	skb->len += length;
	skb->data_len = length;

	/* prepare the deferred event */
	event->endpoint = endpoint;
	event->evt.send_done.lib_cookie = cmd.lib_cookie;
	event->evt.generic.type = MPOE_EVT_SEND_DONE;
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
