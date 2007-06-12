#include <linux/kernel.h>
#include <linux/module.h>

#include "mpoe_common.h"

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

int
mpoe_net_recv(struct sk_buff *skb, struct net_device *ifp, struct packet_type *pt,
	      struct net_device *orig_dev)
{
	struct mpoe_iface *iface;
	struct mpoe_endpoint *endpoint;
	struct mpoe_hdr linear_header;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	int index;
	union mpoe_evt *evt;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		return 0;

	/* len doesn't include header */
	skb_push(skb, ETH_HLEN);

	iface = mpoe_net_iface_from_ifp(ifp);
	if (!iface) {
		printk(KERN_INFO "MPoE: Dropping packets on non MPoE interface\n");
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
		mh = (struct mpoe_hdr *) skb->mac.raw;
	}
	eh = &mh->head.eth;

	index = mh->body.generic.dst_endpoint;
	if (index >= mpoe_endpoint_max || iface->endpoints[index] == NULL) {
		printk(KERN_INFO "MPoE: Dropping packet for unknown endpoint %d\n", index);
		goto exit;
	}
	endpoint = iface->endpoints[index];

	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping packet because of event queue full\n");
		goto exit;
	}

	switch (mh->body.generic.ptype) {
	case MPOE_PKT_TINY: {
		struct mpoe_evt_recv_tiny * event = &evt->tiny;
		mpoe_ethhdr_src_to_mac_addr(&event->src_addr, eh);
		event->src_endpoint = mh->body.tiny.src_endpoint;
		event->length = mh->body.tiny.length;
		event->match_info = (((uint64_t) mh->body.tiny.match_a) << 32) | ((uint64_t) mh->body.tiny.match_b);
		skb_copy_bits(skb, sizeof(struct mpoe_hdr), event->data,
			      mh->body.tiny.length);
		/* check for EFAULT */
		event->type = MPOE_EVT_RECV_TINY;
		break;
	}

	case MPOE_PKT_MEDIUM: {
		struct mpoe_evt_recv_medium * event = &evt->medium;
		char * recvq_slot = mpoe_find_next_recvq_slot(endpoint);
		mpoe_ethhdr_src_to_mac_addr(&evt->medium.src_addr, eh);
		event->src_endpoint = mh->body.medium.msg.src_endpoint;
		event->length = mh->body.medium.msg.length;
		event->match_info = (((uint64_t) mh->body.medium.msg.match_a) << 32) | ((uint64_t) mh->body.medium.msg.match_b);
		event->type = MPOE_EVT_RECV_MEDIUM;
		skb_copy_bits(skb, sizeof(struct mpoe_hdr), recvq_slot,
			      skb->len - sizeof(*mh));
		/* check for EFAULT */
		break;
	}

	case MPOE_PKT_RENDEZ_VOUS: {
		/* FIXME */
		break;
	}

	case MPOE_PKT_PULL: {
		struct mpoe_mac_addr src_addr;
		mpoe_ethhdr_src_to_mac_addr(&src_addr, eh);
		/* FIXME: do not convert twice */
		mpoe_net_pull_reply(endpoint, &mh->body.pull, &src_addr);
		/* FIXME: check return value */
		break;
	}

	case MPOE_PKT_PULL_REPLY: {
		printk("got a pull reply length %d\n", mh->body.pull_reply.length);
		/* FIXME */
		break;
	}

	default: {
		printk(KERN_INFO "MPoE: Dropping packing with unrecognized type %d\n", mh->body.generic.ptype);
		goto exit;
	}
	}

//	printk(KERN_INFO "MPoE: got packet type %d length %d matching 0x%llx for endpoint %d from %d\n",
//	       mh->ptype, mh->length, mh->match_info, mh->dst_endpoint, mh->src_endpoint);

 exit:
	/* TODO: send nack */
	dev_kfree_skb(skb);
	return 0;
}
