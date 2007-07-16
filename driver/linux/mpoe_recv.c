#include <linux/kernel.h>
#include <linux/module.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

/******************************
 * Manage event and data slots
 */

union mpoe_evt *
mpoe_find_next_eventq_slot(struct mpoe_endpoint *endpoint)
{
	/* FIXME: need locking */
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

static int
mpoe_recv_tiny(struct mpoe_iface * iface,
	       struct mpoe_hdr * mh,
	       struct sk_buff * skb)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_msg *tiny = &mh->body.tiny;
	uint16_t length = tiny->length;
	union mpoe_evt *evt;
	struct mpoe_evt_recv_tiny *event;
	int err = 0;

	/* check packet length */
	if (length > MPOE_TINY_MAX) {
		printk(KERN_DEBUG "MPoE: Dropping too long TINY packet (length %d)\n",
		       (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (length != skb->len - sizeof(struct mpoe_hdr)) {
		printk(KERN_DEBUG "MPoE: Dropping TINY packet with %ld bytes instead of %d\n",
		       (unsigned long) skb->len - sizeof(struct mpoe_hdr), (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* get the destination endpoint */
	endpoint = mpoe_endpoint_acquire_by_iface_index(iface, tiny->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping TINY packet for unknown endpoint %d\n",
		       tiny->dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping TINY packet because of event queue full\n");
		err = -EBUSY;
		goto out_with_endpoint;
	}
	event = &evt->recv_tiny;

	/* fill event */
	mpoe_ethhdr_src_to_mac_addr(&event->src_addr, eh);
	event->src_endpoint = tiny->src_endpoint;
	event->length = length;
	event->match_info = MPOE_MATCH_INFO_FROM_PKT(tiny);
	event->seqnum = tiny->lib_seqnum;

#ifdef MPOE_DEBUG
	printk("MPoE: received TINY %d from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       length,
	       eh->h_source[0], eh->h_source[1], eh->h_source[2],
	       eh->h_source[3], eh->h_source[4], eh->h_source[5],
	       eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],
	       eh->h_dest[3], eh->h_dest[4], eh->h_dest[5]);
#endif
	/* copy data in event data */
	err = skb_copy_bits(skb, sizeof(struct mpoe_hdr), event->data, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_RECV_TINY;

	mpoe_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	mpoe_endpoint_release(endpoint);
 out:
	return err;
}

static int
mpoe_recv_small(struct mpoe_iface * iface,
		struct mpoe_hdr * mh,
		struct sk_buff * skb)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_msg *small = &mh->body.small;
	uint16_t length = small->length;
	union mpoe_evt *evt;
	struct mpoe_evt_recv_small *event;
	char *recvq_slot;
	int err;

	/* check packet length */
	if (length > MPOE_SMALL_MAX) {
		printk(KERN_DEBUG "MPoE: Dropping too long SMALL packet (length %d)\n",
		       (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (length != skb->len - sizeof(struct mpoe_hdr)) {
		printk(KERN_DEBUG "MPoE: Dropping SMALL packet with %ld bytes instead of %d\n",
		       (unsigned long) skb->len - sizeof(struct mpoe_hdr), (unsigned) length);
		err = -EINVAL;
		goto out;
	}

	/* get the destination endpoint */
	endpoint = mpoe_endpoint_acquire_by_iface_index(iface, small->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping SMALL packet for unknown endpoint %d\n",
		       small->dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping SMALL packet because of event queue full\n");
		err = -EBUSY;
		goto out_with_endpoint;
	}
	event = &evt->recv_small;

	/* fill event */
	mpoe_ethhdr_src_to_mac_addr(&event->src_addr, eh);
	event->src_endpoint = small->src_endpoint;
	event->length = length;
	event->match_info = MPOE_MATCH_INFO_FROM_PKT(small);
	event->seqnum = small->lib_seqnum;

#ifdef MPOE_DEBUG
	printk("MPoE: received SMALL %d from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       length,
	       eh->h_source[0], eh->h_source[1], eh->h_source[2],
	       eh->h_source[3], eh->h_source[4], eh->h_source[5],
	       eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],
	       eh->h_dest[3], eh->h_dest[4], eh->h_dest[5]);
#endif

	/* copy data in recvq slot */
	recvq_slot = mpoe_find_next_recvq_slot(endpoint);
	err = skb_copy_bits(skb, sizeof(struct mpoe_hdr), recvq_slot, length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_RECV_SMALL;

	mpoe_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	mpoe_endpoint_release(endpoint);
 out:
	return err;
}

static int
mpoe_recv_medium_frag(struct mpoe_iface * iface,
		      struct mpoe_hdr * mh,
		      struct sk_buff * skb)
{
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_medium_frag *medium = &mh->body.medium;
	uint16_t frag_length = medium->frag_length;
	union mpoe_evt *evt;
	struct mpoe_evt_recv_medium *event;
	char *recvq_slot;
	int err;

	/* check packet length */
	if (frag_length > MPOE_RECVQ_ENTRY_SIZE) {
		printk(KERN_DEBUG "MPoE: Dropping too long MEDIUM fragment packet (length %d)\n",
		       (unsigned) frag_length);
		err = -EINVAL;
		goto out;
	}

	/* check actual data length */
	if (frag_length != skb->len - sizeof(struct mpoe_hdr)) {
		printk(KERN_DEBUG "MPoE: Dropping MEDIUM fragment with %ld bytes instead of %d\n",
		       (unsigned long) skb->len - sizeof(struct mpoe_hdr), (unsigned) frag_length);
		err = -EINVAL;
		goto out;
	}

	/* get the destination endpoint */
	endpoint = mpoe_endpoint_acquire_by_iface_index(iface, medium->msg.dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping MEDIUM packet for unknown endpoint %d\n",
		       medium->msg.dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping MEDIUM packet because of event queue full\n");
		err = -EBUSY;
		goto out_with_endpoint;
	}
	event = &evt->recv_medium;

	/* fill event */
	mpoe_ethhdr_src_to_mac_addr(&event->src_addr, eh);
	event->src_endpoint = medium->msg.src_endpoint;
	event->match_info = MPOE_MATCH_INFO_FROM_PKT(&medium->msg);
	event->msg_length = medium->msg.length;
	event->seqnum = medium->msg.lib_seqnum;
	event->frag_length = frag_length;
	event->frag_seqnum = medium->frag_seqnum;
	event->frag_pipeline = medium->frag_pipeline;

#ifdef MPOE_DEBUG
	printk("MPoE: received MEDIUM FRAG %d from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frag_length,
	       eh->h_source[0], eh->h_source[1], eh->h_source[2],
	       eh->h_source[3], eh->h_source[4], eh->h_source[5],
	       eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],
	       eh->h_dest[3], eh->h_dest[4], eh->h_dest[5]);
#endif
	/* copy data in recvq slot */
	recvq_slot = mpoe_find_next_recvq_slot(endpoint);
	err = skb_copy_bits(skb, sizeof(struct mpoe_hdr), recvq_slot, frag_length);
	/* cannot fail since pages are allocated by us */
	BUG_ON(err < 0);

	/* set the type at the end so that user-space does not find the slot on error */
	event->type = MPOE_EVT_RECV_MEDIUM;

	mpoe_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	mpoe_endpoint_release(endpoint);
 out:
	return err;
}

static int
mpoe_recv_rndv(struct mpoe_iface * iface,
	       struct mpoe_hdr * mh,
	       struct sk_buff * skb)
{
#if 0
	struct mpoe_endpoint * endpoint;
	struct ethhdr *eh = &mh->head.eth;
	struct mpoe_pkt_rndv *rndv = &mh->body.rndv;

	/* get the destination endpoint */
	endpoint = mpoe_endpoint_acquire_by_iface_index(iface, rndv->dst_endpoint);
	if (!endpoint) {
		printk(KERN_DEBUG "MPoE: Dropping RNDV packet for unknown endpoint %d\n",
		       rndv->dst_endpoint);
		err = -EINVAL;
		goto out;
	}

	/* get the eventq slot */
	evt = mpoe_find_next_eventq_slot(endpoint);
	if (!evt) {
		printk(KERN_INFO "MPoE: Dropping RNDV packet because of event queue full\n");
		err = -EBUSY;
		goto out_with_endpoint;
	}
	event = &evt->rndv;

	mpoe_endpoint_release(endpoint);

	return 0;

 out_with_endpoint:
	mpoe_endpoint_release(endpoint);
 out:
	return err;
#endif

	/* FIXME */

	return 0;
}

static int
mpoe_recv_nosys(struct mpoe_iface * iface,
		struct mpoe_hdr * mh,
		struct sk_buff * skb)
{
	printk(KERN_DEBUG "MPoE: Dropping packing with unsupported type %d\n",
	       mh->body.generic.ptype);

	return 0;
}

static int
mpoe_recv_error(struct mpoe_iface * iface,
		struct mpoe_hdr * mh,
		struct sk_buff * skb)
{
	printk(KERN_DEBUG "MPoE: Dropping packing with unrecognized type %d\n",
	       mh->body.generic.ptype);

	return 0;
}

/***********************
 * Packet type handlers
 */

static int (*mpoe_pkt_type_handlers[MPOE_PKT_TYPE_MAX+1])(struct mpoe_iface * iface, struct mpoe_hdr * mh, struct sk_buff * skb);

void
mpoe_pkt_type_handlers_init(void)
{
	int i;

	for(i=0; i<=MPOE_PKT_TYPE_MAX; i++)
		mpoe_pkt_type_handlers[i] = mpoe_recv_error;

	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_RAW] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_MFM_NIC_REPLY] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_HOST_QUERY] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_HOST_REPLY] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_ETHER_UNICAST] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_ETHER_MULTICAST] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_ETHER_NATIVE] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_TRUC] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_CONNECT] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_TINY] = mpoe_recv_tiny;
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_SMALL] = mpoe_recv_small;
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_MEDIUM] = mpoe_recv_medium_frag;
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_RENDEZ_VOUS] = mpoe_recv_rndv;
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_PULL] = mpoe_recv_pull;
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_PULL_REPLY] = mpoe_recv_pull_reply;
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_NOTIFY] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_NACK_LIB] = mpoe_recv_nosys; /* FIXME */
	mpoe_pkt_type_handlers[MPOE_PKT_TYPE_NACK_MCP] = mpoe_recv_nosys; /* FIXME */
}

/***********************
 * Main receive routine
 */

static int
mpoe_recv(struct sk_buff *skb, struct net_device *ifp, struct packet_type *pt,
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

	iface = mpoe_iface_find_by_ifp(ifp);
	if (!iface) {
		printk(KERN_DEBUG "MPoE: Dropping packets on non MPoE interface\n");
		goto out;
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

	/* no need to check ptype since there is a default error handler
	 * for all erroneous values
	 */
	mpoe_pkt_type_handlers[mh->body.generic.ptype](iface, mh, skb);

 out:
	/* FIXME: send nack */
	dev_kfree_skb(skb);
	return 0;
}

struct packet_type mpoe_pt = {
	.type = __constant_htons(ETH_P_MPOE),
	.func = mpoe_recv,
};

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
