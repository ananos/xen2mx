#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>

#include <asm/semaphore.h>

#include "mpoe_common.h"
#include "mpoe_io.h"
#include "mpoe_hal.h"
#include "mpoe_wire.h"

/* Forward declarations */
static int mpoe_net_pull_reply(struct mpoe_endpoint *, struct mpoe_pkt_pull_request *, uint8_t dest_mac[6]);

/*************
 * Finding, attaching, detaching interfaces
 */

/* returns an interface hold matching ifname */
static struct net_device *
mpoe_net_find_iface_by_name(const char * ifname)
{
	struct net_device * ifp;

        read_lock(&dev_base_lock);
        for (ifp = dev_base; ifp != NULL; ifp = ifp->next) {
		dev_hold(ifp);
		if (!strcmp(ifp->name, ifname)) {
		        read_unlock(&dev_base_lock);
			return ifp;
		}
		dev_put(ifp);
	}
        read_unlock(&dev_base_lock);

	printk(KERN_ERR "MPoE: Failed to find interface '%s'\n", ifname);
	return NULL;
}

static struct mpoe_iface ** mpoe_ifaces;
static unsigned int mpoe_iface_nr = 0;
static DECLARE_MUTEX_LOCKED(mpoe_iface_mutex);

/* called with interface hold */
static int
mpoe_net_attach_iface(struct net_device * ifp)
{
	struct mpoe_iface * iface;
	int ret;
	int i;

	if (mpoe_iface_nr == mpoe_iface_max) {
		printk(KERN_ERR "MPoE: Too many interfaces already attached\n");
		ret = -EBUSY;
		goto out_with_ifp_hold;
	}

	/* TODO: do not attach twice ? */

	for(i=0; i<mpoe_iface_max; i++)
		if (mpoe_ifaces[i] == NULL)
			break;

	iface = kzalloc(sizeof(struct mpoe_iface), GFP_KERNEL);
	if (!iface) {
		printk(KERN_ERR "MPoE: Failed to allocate interface as board %d\n", i);
		ret = -ENOMEM;
		goto out_with_ifp_hold;
	}

	printk(KERN_INFO "MPoE: Attaching interface '%s' as #%i\n", ifp->name, i);

	iface->eth_ifp = ifp;
	iface->endpoint_nr = 0;
	iface->endpoints = kzalloc(mpoe_endpoint_max * sizeof(struct mpoe_endpoint *), GFP_KERNEL);
	if (!iface->endpoints) {
		printk(KERN_ERR "MPoE: Failed to allocate interface endpoint pointers\n");
		ret = -ENOMEM;
		goto out_with_iface;
	}

	spin_lock_init(&iface->endpoint_lock);
	iface->index = i;
	mpoe_iface_nr++;
	mpoe_ifaces[i] = iface;

	return 0;

 out_with_iface:
	kfree(iface);
 out_with_ifp_hold:
	dev_put(ifp);
	return ret;
}

/* called with interface hold */
static int
mpoe_net_detach_iface(struct mpoe_iface * iface)
{
	if (iface->endpoint_nr) {
		printk(KERN_INFO "MPoE: cannot detach interface #%d '%s', still %d endpoints open\n",
		       iface->index, iface->eth_ifp->name, iface->endpoint_nr);
		return -EBUSY;
	}

	printk(KERN_INFO "MPoE: detaching interface #%d '%s'\n", iface->index, iface->eth_ifp->name);

	BUG_ON(mpoe_ifaces[iface->index] == NULL);
	mpoe_ifaces[iface->index] = NULL;
	mpoe_iface_nr--;
	kfree(iface->endpoints);
	dev_put(iface->eth_ifp);
	kfree(iface);

	return 0;
}

/* list attached interfaces */
int
mpoe_net_ifaces_show(char *buf)
{
	int total = 0;
	int i;

	down(&mpoe_iface_mutex);
	for (i=0; i<mpoe_iface_max; i++) {
		struct mpoe_iface * iface = mpoe_ifaces[i];
		if (iface) {
			char * ifname = iface->eth_ifp->name;
			int length = strlen(ifname);
			/* TODO: check total+length+2 <= PAGE_SIZE ? */
			strcpy(buf, ifname);
			buf += length;
			strcpy(buf, "\n");
			buf += 1;
			total += length+1;
		}
	}
	up(&mpoe_iface_mutex);

	return total + 1;
}

/* +name add an interface, -name removes one */
int
mpoe_net_ifaces_store(const char *buf, size_t size)
{
	char copy[IFNAMSIZ];
	char * ptr;

	/* remove the ending \n if required, so copy first since buf is const */
	strncpy(copy, buf+1, IFNAMSIZ);
	copy[IFNAMSIZ-1] = '\0';
	ptr = strchr(copy, '\n');
	if (ptr)
		*ptr = '\0';

	if (buf[0] == '-') {
		int i, found = 0;
		/* in case none matches, we return -EINVAL. if one matches, it sets ret accordingly */
		int ret = -EINVAL;

		down(&mpoe_iface_mutex);
		for(i=0; i<mpoe_iface_max; i++) {
			struct mpoe_iface * iface = mpoe_ifaces[i];
			if (iface != NULL && !strcmp(iface->eth_ifp->name, copy)) {
				ret = mpoe_net_detach_iface(iface);
				if (!ret)
					found = 1;
				break;
			}
		}
		up(&mpoe_iface_mutex);

		if (!found) {
			printk(KERN_ERR "MPoE: Cannot find any attached interface '%s' to detach\n", copy);
			return -EINVAL;
		}
		return size;

	} else if (buf[0] == '+') {
		struct net_device * ifp;
		int ret;

		ifp = mpoe_net_find_iface_by_name(copy);
		if (!ifp)
			return -EINVAL;

		down(&mpoe_iface_mutex);
		ret = mpoe_net_attach_iface(ifp);
		up(&mpoe_iface_mutex);
		if (ret < 0)
			return ret;

		return size;

	} else {
		printk(KERN_ERR "MPoE: Unrecognized command passed in the ifaces file, need either +name or -name\n");
		return -EINVAL;
	}
}

static struct mpoe_iface *
mpoe_net_iface_from_ifp(struct net_device *ifp)
{
	int i;

	for (i=0; i<mpoe_iface_max; i++) {
		struct mpoe_iface * iface = mpoe_ifaces[i];
		if (iface && iface->eth_ifp == ifp)
			return iface;
	}

	return NULL;
}

int
mpoe_net_get_iface_count(void)
{
	int i, count = 0;

	for (i=0; i<mpoe_iface_max; i++)
		if (mpoe_ifaces[i] != NULL)
			count++;

	return count;
}

int
mpoe_net_get_iface_id(uint8_t board_index, struct mpoe_mac_addr * board_addr, char * board_name)
{
	struct net_device * ifp;

	if (board_index >= mpoe_iface_max
	    || mpoe_ifaces[board_index] == NULL)
		return -EINVAL;

	ifp = mpoe_ifaces[board_index]->eth_ifp;

	mpoe_mac_addr_of_netdevice(ifp, board_addr);
	strncpy(board_name, ifp->name, MPOE_IF_NAMESIZE);

	return 0;
}

/**********
 * Attaching endpoints to boards
 */

int
mpoe_net_attach_endpoint(struct mpoe_endpoint * endpoint, uint8_t board_index, uint8_t endpoint_index)
{
	struct mpoe_iface * iface;

	down(&mpoe_iface_mutex);
	if (board_index >= mpoe_iface_max || mpoe_ifaces[board_index] == NULL) {
		printk(KERN_ERR "MPoE: Cannot open endpoint on unexisting board %d\n", board_index);
		up(&mpoe_iface_mutex);
		return -EINVAL;
	}

	iface = mpoe_ifaces[board_index];

	if (endpoint_index >= mpoe_endpoint_max || iface->endpoints[endpoint_index] != NULL) {
		printk(KERN_ERR "MPoE: Cannot open busy endpoint %d\n", endpoint_index);
		up(&mpoe_iface_mutex);
		return -EBUSY;
	}

	endpoint->iface = iface;
	endpoint->board_index = board_index;
	endpoint->endpoint_index = endpoint_index;

	spin_lock(&iface->endpoint_lock);
	iface->endpoint_nr++;
	iface->endpoints[endpoint_index] = endpoint ;
	spin_unlock(&iface->endpoint_lock);
	up(&mpoe_iface_mutex);

	return 0;
}

void
mpoe_net_detach_endpoint(struct mpoe_endpoint * endpoint)
{
	struct mpoe_iface * iface = endpoint->iface;
	int index = endpoint->endpoint_index;

	spin_lock(&iface->endpoint_lock);
	iface->endpoint_nr--;
	BUG_ON(iface->endpoints[index] == NULL);
	iface->endpoints[index] = NULL;
	spin_unlock(&iface->endpoint_lock);
}

/*************
 * Receiving packets
 */

static struct sk_buff *
mpoe_new_skb(struct net_device *ifp, unsigned long len)
{
	struct sk_buff *skb;

	skb = mpoe_netdev_alloc_skb(ifp, len);
	if (skb) {
		skb->nh.raw = skb->mac.raw = skb->data;
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

static int
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
		mpoe_net_pull_reply(endpoint, &mh->body.pull, eh->h_source);
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

static struct packet_type mpoe_pt = {
	.type = __constant_htons(ETH_P_MPOE),
	.func = mpoe_net_recv,
};

/*********
 * Sending
 */

int
mpoe_net_send_tiny(struct mpoe_endpoint * endpoint,
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
	mh = (struct mpoe_hdr *) skb->mac.raw;
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
mpoe_net_send_medium(struct mpoe_endpoint * endpoint,
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
	mh = (struct mpoe_hdr *) skb->mac.raw;
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
mpoe_net_send_rendez_vous(struct mpoe_endpoint * endpoint,
			  void __user * uparam)
{
	return -ENOSYS;
}

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
	mh = (struct mpoe_hdr *) skb->mac.raw;
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

static int
mpoe_net_pull_reply(struct mpoe_endpoint * endpoint,
		    struct mpoe_pkt_pull_request * pull_request, uint8_t dest_mac[6])
{
	struct sk_buff *skb;
	struct mpoe_hdr *mh;
	struct ethhdr *eh;
	struct mpoe_iface * iface = endpoint->iface;
	struct net_device * ifp = iface->eth_ifp;
	struct mpoe_user_region *region;
	uint32_t rdma_id;
	int ret;
	uint32_t length, queued, iseg;

	skb = mpoe_new_skb(ifp, sizeof(*mh));
	if (skb == NULL) {
		printk(KERN_INFO "MPoE: Failed to create pull reply skb\n");
		ret = -ENOMEM;
		goto out;
	}

	/* locate headers */
	mh = (struct mpoe_hdr *) skb->mac.raw;
	eh = &mh->head.eth;

	/* fill ethernet header */
	memset(eh, 0, sizeof(*eh));
	memcpy(eh->h_dest, dest_mac, sizeof (eh->h_dest));
	memcpy(eh->h_source, ifp->dev_addr, sizeof (eh->h_source));
	eh->h_proto = __constant_cpu_to_be16(ETH_P_MPOE);

	/* fill mpoe header */
	mh->body.pull_reply.puller_rdma_id = pull_request->puller_rdma_id;
	mh->body.pull_reply.puller_offset = pull_request->puller_offset;
	mh->body.pull_reply.ptype = MPOE_PKT_PULL_REPLY;

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

	mh->body.pull_reply.length = queued;

	dev_queue_xmit(skb);

//	printk(KERN_INFO "MPoE: sent a pull reply from endpoint %d\n",
//	       endpoint->endpoint_index);

	return 0;

 out_with_region:
	spin_unlock(&endpoint->user_regions_lock);
 out_with_skb:
	dev_kfree_skb(skb);
 out:
	return ret;
}

/*************
 * Netdevice notifier
 */

static int
mpoe_netdevice_notifier_cb(struct notifier_block *unused,
				      unsigned long event, void *ptr)
{
        struct net_device *ifp = (struct net_device *) ptr;

        if (event == NETDEV_UNREGISTER) {
		int i;
		for (i=0; i<mpoe_iface_max; i++) {
			struct mpoe_iface * iface = mpoe_ifaces[i];
			if (iface && iface->eth_ifp == ifp) {
				int ret;
				int j;
				printk(KERN_INFO "MPoE: interface '%s' being unregistered, forcing closing of endpoints...\n",
				       ifp->name);
				for(j=0; j<mpoe_endpoint_max; j++) {
					struct mpoe_endpoint * endpoint = iface->endpoints[j];
					if (endpoint)
						mpoe_close_endpoint(endpoint, NULL);
				}
				ret = mpoe_net_detach_iface(iface);
				BUG_ON(ret);
			}
		}
	}

        return NOTIFY_DONE;
}

static struct notifier_block mpoe_netdevice_notifier = {
        .notifier_call = mpoe_netdevice_notifier_cb,
};

/*************
 * Initialization and termination
 */

int
mpoe_net_init(const char * ifnames)
{
	int ret = 0;

	dev_add_pack(&mpoe_pt);

	ret = register_netdevice_notifier(&mpoe_netdevice_notifier);
	if (ret < 0) {
		printk(KERN_ERR "MPoE: failed to register netdevice notifier\n");
		goto abort_with_pack;
	}

	mpoe_ifaces = kzalloc(mpoe_iface_max * sizeof(struct mpoe_iface *), GFP_KERNEL);
	if (!mpoe_ifaces) {
		printk(KERN_ERR "MPoE: failed to allocate interface array\n");
		ret = -ENOMEM;
		goto abort_with_notifier;
	}

	if (ifnames) {
		/* attach ifaces whose name are in ifnames (limited to mpoe_iface_max) */
		char * copy = kstrdup(ifnames, GFP_KERNEL);
		char * ifname;

		while ((ifname = strsep(&copy, ",")) != NULL) {
			struct net_device * ifp;
			ifp = mpoe_net_find_iface_by_name(ifname);
			if (ifp)
				if (mpoe_net_attach_iface(ifp) < 0)
					break;
		}

		kfree(copy);

	} else {
		/* attach everything (limited to mpoe_iface_max) */
		struct net_device * ifp;

	        read_lock(&dev_base_lock);
        	for (ifp = dev_base; ifp != NULL; ifp = ifp->next) {
			dev_hold(ifp);
			if (mpoe_net_attach_iface(ifp) < 0)
				break;
		}
	        read_unlock(&dev_base_lock);
	}
	up(&mpoe_iface_mutex); /* has been initialized locked */

	printk(KERN_INFO "MPoE: attached %d interfaces\n", mpoe_iface_nr);
	return 0;

 abort_with_notifier:
	unregister_netdevice_notifier(&mpoe_netdevice_notifier);
 abort_with_pack:
	dev_remove_pack(&mpoe_pt);
	return ret;
}

void
mpoe_net_exit(void)
{
	int i, nr = 0;

	down(&mpoe_iface_mutex); /* should not be needed, since all other users
				  * have a reference to the chardev and thus prevent
				  * the module from being unloaded */

	for (i=0; i<mpoe_iface_max; i++) {
		struct mpoe_iface * iface = mpoe_ifaces[i];
		if (iface != NULL) {
			BUG_ON(mpoe_net_detach_iface(iface) < 0);
			nr++;
		}
	}

	printk(KERN_INFO "MPoE: detached %d interfaces\n", nr);

	kfree(mpoe_ifaces);

	unregister_netdevice_notifier(&mpoe_netdevice_notifier);

	dev_remove_pack(&mpoe_pt);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
