#include <linux/kernel.h>
#include <linux/module.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

static inline struct mpoe_endpoint *
mpoe_net_get_dst_endpoint(struct mpoe_iface *iface,
			  uint8_t dst_endpoint)
{
	if (dst_endpoint >= mpoe_endpoint_max
	    || iface->endpoints[dst_endpoint] == NULL)
		return NULL;

	/* FIXME: increase use count while holding a lock */

	return iface->endpoints[dst_endpoint];
}

/******************************
 * Manage event and data slots
 */

static inline union mpoe_evt *
mpoe_find_next_eventq_slot(struct mpoe_endpoint *endpoint)
{
	union mpoe_evt *slot = endpoint->next_eventq_slot;
	if (slot->generic.type != MPOE_EVT_NONE) {
		printk(KERN_INFO "MPoE: Event queue full, no event slot available for endpoint %d\n",
		       endpoint->endpoint_index);
		return NULL;
	}

	endpoint->next_eventq_slot = slot + 1;
	if ((void *) endpoint->next_eventq_slot >= endpoint->eventq + MPOE_EVENTQ_SIZE)
		endpoint->next_eventq_slot = endpoint->eventq;

	/* recvq slot is at same index for now */
	endpoint->next_recvq_slot = endpoint->recvq + ((void *) slot - endpoint->eventq)/sizeof(union mpoe_evt)*PAGE_SIZE;

	return slot;
}

static inline char *
mpoe_find_next_recvq_slot(struct mpoe_endpoint *endpoint)
{
	return endpoint->next_recvq_slot;
}

/***************************
 * Event reporting routines
 */

#define MPOE_MATCH_INFO_FROM_PKT(_pkt) (((uint64_t) (_pkt)->match_a) << 32) | ((uint64_t) (_pkt)->match_b)

static int
mpoe_net_recv_tiny(struct mpoe_iface * iface,
		   struct mpoe_hdr * mh,
		   struct sk_buff * skb)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_msg *tiny = &mh->body.tiny;
	union mpoe_evt *evt;
	struct mpoe_evt_recv_tiny *event;
	int err = 0;

	/* get the destination endpoint */
	endpoint = mpoe_net_get_dst_endpoint(iface, tiny->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping TINY packet for unknown endpoint %d\n",
		       tiny->dst_endpoint);
		err = -EINVAL;
		goto drop;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping TINY packet because of event queue full\n");
		err = -EBUSY;
		goto drop;
	}
	event = &evt->tiny;

	/* fill event */
	mpoe_ethhdr_src_to_mac_addr(&event->src_addr, eh);
	event->src_endpoint = tiny->src_endpoint;
	event->length = tiny->length;
	event->match_info = MPOE_MATCH_INFO_FROM_PKT(tiny);

	/* copy data in event data */
	err = skb_copy_bits(skb, sizeof(struct mpoe_hdr), event->data,
			    tiny->length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_RECV_TINY;

	return 0;

 drop:
	return err;
}

static int
mpoe_net_recv_medium_frag(struct mpoe_iface * iface,
			  struct mpoe_hdr * mh,
			  struct sk_buff * skb)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_medium_frag *medium = &mh->body.medium;
	union mpoe_evt *evt;
	struct mpoe_evt_recv_medium *event;
	char *recvq_slot;
	int err;

	/* get the destination endpoint */
	endpoint = mpoe_net_get_dst_endpoint(iface, medium->msg.dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping MEDIUM packet for unknown endpoint %d\n",
		       medium->msg.dst_endpoint);
		err = -EINVAL;
		goto drop;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping MEDIUM packet because of event queue full\n");
		err = -EBUSY;
		goto drop;
	}
	event = &evt->medium;

	/* fill event */
	mpoe_ethhdr_src_to_mac_addr(&event->src_addr, eh);
	event->src_endpoint = medium->msg.src_endpoint;
	event->length = medium->msg.length;
	event->match_info = MPOE_MATCH_INFO_FROM_PKT(&medium->msg);

	/* copy data in recvq slot */
	recvq_slot = mpoe_find_next_recvq_slot(endpoint);
	err = skb_copy_bits(skb, sizeof(struct mpoe_hdr), recvq_slot,
			    skb->len - sizeof(struct mpoe_hdr));
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_RECV_MEDIUM;

	return 0;

 drop:
	return err;
}

static void
mpoe_net_recv_rndv(struct mpoe_iface * iface,
		   struct mpoe_hdr * mh)
{
#if 0
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_rndv *rndv = &mh->body.rndv;

	/* get the destination endpoint */
	endpoint = mpoe_net_get_dst_endpoint(iface, rndv->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping RNDV packet for unknown endpoint %d\n",
		       rndv->dst_endpoint);
		err = -EINVAL;
		goto drop;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping RNDV packet because of event queue full\n");
		err = -EBUSY;
		goto drop;
	}
	event = &evt->rndv;

	return 0;

 drop:
	return err;
#endif

	/* FIXME */
}

static int
mpoe_net_recv_pull(struct mpoe_iface * iface,
		   struct mpoe_hdr * mh)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_pull_request *pull = &mh->body.pull;
	struct mpoe_mac_addr src_addr;
	int err = 0;

	/* get the destination endpoint */
	endpoint = mpoe_net_get_dst_endpoint(iface, pull->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping PULL packet for unknown endpoint %d\n",
		       pull->dst_endpoint);
		err = -EINVAL;
		goto drop;
	}

	printk("got a pull length %d\n", pull->length);

	mpoe_ethhdr_src_to_mac_addr(&src_addr, eh);
	/* FIXME: do not convert twice */
	mpoe_net_pull_reply(endpoint, pull, &src_addr);
	/* FIXME: check return value */

	return 0;

 drop:
	return err;
}

static int
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

/***********************
 * Main receive routine
 */

int
mpoe_net_recv(struct sk_buff *skb, struct net_device *ifp, struct packet_type *pt,
	      struct net_device *orig_dev)
{
	struct mpoe_iface *iface;
	struct mpoe_hdr linear_header;
	struct mpoe_hdr *mh;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		return 0;

	/* len doesn't include header */
	skb_push(skb, ETH_HLEN);

	iface = mpoe_net_iface_from_ifp(ifp);
	if (!iface) {
		printk(KERN_DEBUG "MPoE: Dropping packets on non MPoE interface\n");
		goto exit;
	}

	/* no need to linearize the whole skb,
	 * but at least the header to make things simple */
	if (skb_headlen(skb) < sizeof(struct mpoe_hdr)) {
		skb_copy_bits(skb, 0, &linear_header,
			      sizeof(struct mpoe_hdr));
		/* check for EFAULT */
		mh = &linear_header;
	} else {
		/* no need to linearize the header */
		mh = mpoe_hdr(skb);
	}

	switch (mh->body.generic.ptype) {
	case MPOE_PKT_TINY:
		mpoe_net_recv_tiny(iface, mh, skb);
		break;

	case MPOE_PKT_MEDIUM:
		mpoe_net_recv_medium_frag(iface, mh, skb);
		break;

	case MPOE_PKT_RENDEZ_VOUS:
		mpoe_net_recv_rndv(iface, mh);
		break;

	case MPOE_PKT_PULL:
		mpoe_net_recv_pull(iface, mh);
		break;

	case MPOE_PKT_PULL_REPLY:
		mpoe_net_recv_pull_reply(iface, mh);
		break;

	default:
		printk(KERN_DEBUG "MPoE: Dropping packing with unrecognized type %d\n",
		       mh->body.generic.ptype);
		goto exit;
	}

//	printk(KERN_INFO "MPoE: got packet type %d length %d matching 0x%llx for endpoint %d from %d\n",
//	       mh->ptype, mh->length, mh->match_info, mh->dst_endpoint, mh->src_endpoint);

 exit:
	/* TODO: send nack */
	dev_kfree_skb(skb);
	return 0;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
