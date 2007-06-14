#include <linux/kernel.h>
#include <linux/module.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

int
mpoe_net_send_pull(struct mpoe_endpoint * endpoint,
		   void __user * uparam)
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_cmd_send_pull_hdr cmd_hdr;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	int ret;

	ret = copy_from_user(&cmd_hdr, uparam, sizeof(cmd_hdr));
	if (ret) {
		printk(KERN_ERR "MPoE: Failed to read send pull cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	skb = mpoe_new_skb(ifp, sizeof(*mh));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create pull skb\n");
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
	mh->body.pull.src_endpoint = endpoint->endpoint_index;
	mh->body.pull.dst_endpoint = cmd_hdr.dest_endpoint;
	mh->body.pull.ptype = MPOE_PKT_PULL;
	mh->body.pull.length = cmd_hdr.length;
	mh->body.pull.puller_rdma_id = cmd_hdr.local_rdma_id;
	mh->body.pull.puller_offset = cmd_hdr.local_offset;
	mh->body.pull.pulled_rdma_id = cmd_hdr.remote_rdma_id;
	mh->body.pull.pulled_offset = cmd_hdr.remote_offset;

	dev_queue_xmit(skb);

//	printk(KERN_INFO "MPoE: sent a pull message from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out:
	return ret;
}

static inline int
mpoe_net_pull_reply_append_user_region_segment(struct sk_buff *skb,
					       struct mpoe_user_region_segment *seg)
{
	return -ENOSYS;
}

int
mpoe_net_recv_pull(struct mpoe_iface * iface,
		   struct mpoe_hdr * pull_mh)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *pull_eh = &pull_mh->head.eth;
	struct mpoe_pkt_pull_request *pull_request = &pull_mh->body.pull;
	struct sk_buff *skb;
	struct mpoe_hdr *reply_mh;
	struct ethhdr *reply_eh;
	struct net_device * ifp = iface->eth_ifp;
	struct mpoe_user_region *region;
	uint32_t rdma_id, length, queued, iseg;
	int err = 0;

	/* get the destination endpoint */
	endpoint = mpoe_net_get_dst_endpoint(iface, pull_request->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping PULL packet for unknown endpoint %d\n",
		       pull_request->dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	printk("got a pull length %d\n", pull_request->length);

	skb = mpoe_new_skb(ifp, sizeof(*reply_mh));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create pull reply skb\n");
		err = -ENOMEM;
		goto out;
	}

	/* locate headers */
	reply_mh = mpoe_hdr(skb);
	reply_eh = &reply_mh->head.eth;

	/* fill ethernet header */
	memcpy(reply_eh->h_source, ifp->dev_addr, sizeof (reply_eh->h_source));
	reply_eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);
	/* get the destination address */
	memcpy(reply_eh->h_dest, pull_eh->h_source, sizeof(reply_eh->h_dest));

	/* fill mpoe header */
	reply_mh->body.pull_reply.puller_rdma_id = pull_request->puller_rdma_id;
	reply_mh->body.pull_reply.puller_offset = pull_request->puller_offset;
	reply_mh->body.pull_reply.ptype = MPOE_PKT_PULL_REPLY;

	/* get the rdma window */
	rdma_id = pull_request->pulled_rdma_id;
	if (rdma_id >= MPOE_USER_REGION_MAX) {
		printk(KERN_ERR "MPoE: got pull request for invalid window %d\n", rdma_id);
		/* FIXME: send nack */
		goto out_with_skb;
	}
	spin_lock(&endpoint->user_regions_lock);
	region = endpoint->user_regions[rdma_id];

	/* append segment pages */
	queued = 0;
#if 0
	for(iseg = 0;
	    iseg < region->nr_segments && queued < pull_request->length;
	    iseg++) {
		struct mpoe_user_region_segment *segment = &region->segments[iseg];
		uint32_t append;
		append = mpoe_net_pull_reply_append_user_region_segment(skb, segment);
		if (append < 0) {
			printk(KERN_ERR "MPoE: failed to queue segment to skb, error %d\n", append);
			/* FIXME: release pages */
			goto out_with_region;
		}
		queued += append;
	}
#endif
	spin_unlock(&endpoint->user_regions_lock);

	reply_mh->body.pull_reply.length = queued;

	dev_queue_xmit(skb);

//	printk(KERN_INFO "MPoE: sent a pull reply from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out_with_region:
	spin_unlock(&endpoint->user_regions_lock);
 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return err;
}

int
mpoe_net_recv_pull_reply(struct mpoe_iface * iface,
			 struct mpoe_hdr * mh)
{
#if 0
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
#endif
	struct mpoe_pkt_pull_reply *pull_reply = &mh->body.pull_reply;
	int err = 0;

	printk("got a pull reply length %d\n", pull_reply->length);
	/* FIXME */

#if 0
	/* get the destination endpoint */
	endpoint = mpoe_net_get_dst_endpoint(iface, pull_reply->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping PULL REPLY packet for unknown endpoint %d\n",
		       pull_reply->dst_endpoint);
		err = -EINVAL;
		goto drop;
	}

	return 0;

 drop:
#endif
	return err;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
