#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

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
	}
	return skb;
}

int
mpoe_send_tiny(struct mpoe_endpoint * endpoint,
	       void __user * uparam)
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_cmd_send_tiny_hdr cmd_hdr;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	int ret;
	uint8_t length;

	ret = copy_from_user(&cmd_hdr, &((struct mpoe_cmd_send_tiny __user *) uparam)->hdr, sizeof(cmd_hdr));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send tiny cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	skb = mpoe_new_skb(ifp,
			   sizeof(struct mpoe_hdr) + cmd_hdr.length);
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
	mpoe_mac_addr_to_ethhdr_dst(&cmd_hdr.dest_addr, eh);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

	/* fill mpoe header */
	mh->body.tiny.src_endpoint = endpoint->endpoint_index;
	mh->body.tiny.dst_endpoint = cmd_hdr.dest_endpoint;
	mh->body.tiny.ptype = MPOE_PKT_TINY;
	length = mh->body.tiny.length = cmd_hdr.length;
	/* mh->offset useless for tiny */
	mh->body.tiny.match_a = cmd_hdr.match_info >> 32;
	mh->body.tiny.match_b = cmd_hdr.match_info & 0xffffffff;

	/* fill data */
	if (length > MPOE_TINY_MAX) {
		printk(KERN_ERR "MPoE: Cannot send more than %d as a tiny (tried %d)\n",
		       MPOE_TINY_MAX, length);
		ret = -EINVAL;
		goto out_with_skb;
	}

	/* copy the data right after the header */
	ret = copy_from_user(mh+1, &((struct mpoe_cmd_send_tiny __user *) uparam)->data, length);
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		goto out_with_skb;
	}

	dev_queue_xmit(skb);

//	printk(KERN_INFO "MPoE: sent a tiny message from endpoint %d\n",
//	       endpoint->endpoint_index);

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
	struct mpoe_cmd_send_medium_hdr cmd_hdr;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct page * page;
	int ret;
	uint32_t length;

	ret = copy_from_user(&cmd_hdr, uparam, sizeof(cmd_hdr));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send medium cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	skb = mpoe_new_skb(ifp, sizeof(*mh));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create medium skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = mpoe_hdr(skb);
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	mpoe_mac_addr_to_ethhdr_dst(&cmd_hdr.dest_addr, eh);
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

	/* fill mpoe header */
	mh->body.medium.msg.src_endpoint = endpoint->endpoint_index;
	mh->body.medium.msg.dst_endpoint = cmd_hdr.dest_endpoint;
	mh->body.medium.msg.ptype = MPOE_PKT_MEDIUM;
	length = mh->body.medium.msg.length = cmd_hdr.length;
	mh->body.medium.msg.match_a = cmd_hdr.match_info >> 32;
	mh->body.medium.msg.match_b = cmd_hdr.match_info & 0xffffffff;

	/* fill data */
	if (length > PAGE_SIZE) { /* FIXME */
		printk(KERN_ERR "MPoE: Cannot send more than %ld as a medium (tried %ld)\n",
		       PAGE_SIZE * 1UL, (unsigned long) length);
		ret = -EINVAL;
		goto out_with_skb;
	}

	/* append sendq page */
	page = vmalloc_to_page(endpoint->sendq + (cmd_hdr.sendq_page_offset << PAGE_SHIFT));
	BUG_ON(page == NULL);
	get_page(page);
	skb_fill_page_desc(skb, 0, page, 0, length);
	skb->len += length;
	skb->data_len = length;

	dev_queue_xmit(skb);

//	printk(KERN_INFO "MPoE: sent a medium message from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out_with_skb:
	dev_kfree_skb(skb);
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
