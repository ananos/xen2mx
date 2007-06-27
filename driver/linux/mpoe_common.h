#ifndef __mpoe_common_h__
#define __mpoe_common_h__

#include <linux/fs.h>
#include <linux/netdevice.h>

#include "mpoe_wire.h"
#include "mpoe_io.h"
/* FIXME: assertion to check MPOE_IF_NAMESIZE == IFNAMSIZ */

extern int mpoe_iface_max;
extern int mpoe_endpoint_max;

extern int mpoe_net_init(const char * ifnames);
extern void mpoe_net_exit(void);

struct mpoe_iface {
	int index;

	struct net_device * eth_ifp;

	spinlock_t endpoint_lock;
	int endpoint_nr;
	struct mpoe_endpoint ** endpoints;
};

struct mpoe_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;

	atomic_t refcount;
	wait_queue_head_t noref_queue;
	int closing : 1;

	struct mpoe_iface * iface;

	void * sendq, * recvq, * eventq;
	union mpoe_evt * next_eventq_slot;
	char * next_recvq_slot;

	struct file * file;

	spinlock_t user_regions_lock;
	struct mpoe_user_region * user_regions[MPOE_USER_REGION_MAX];
};

struct mpoe_user_region {
	unsigned int nr_segments;
	struct mpoe_user_region_segment {
		unsigned int offset;
		unsigned long length;
		unsigned long nr_pages;
		struct page ** pages;
	} segments[0];
};

/* manage endpoints */
extern int mpoe_endpoint_attach(struct mpoe_endpoint * endpoint, uint8_t board_index, uint8_t endpoint_index);
extern void mpoe_endpoint_detach(struct mpoe_endpoint * endpoint);
extern int mpoe_endpoint_close(struct mpoe_endpoint * endpoint, void __user * dummy);
extern struct mpoe_endpoint * mpoe_endpoint_acquire(struct mpoe_iface *iface, uint8_t dst_endpoint);
extern void mpoe_endpoint_release(struct mpoe_endpoint * endpoint);

/* manage ifaces */
extern int mpoe_ifaces_show(char *buf);
extern int mpoe_ifaces_store(const char *buf, size_t size);
extern int mpoe_ifaces_get_count(void);
extern int mpoe_iface_get_id(uint8_t board_index, struct mpoe_mac_addr * board_addr, char * board_name);
extern struct mpoe_iface * mpoe_iface_find_by_ifp(struct net_device *ifp);

/* sending */
extern struct sk_buff * mpoe_new_skb(struct net_device *ifp, unsigned long len);
extern int mpoe_send_tiny(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_send_medium(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_send_rendez_vous(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_send_pull(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_pull_reply(struct mpoe_endpoint * endpoint, struct mpoe_pkt_pull_request * pull_request, struct mpoe_mac_addr * dest_addr);

/* receiving */
extern struct packet_type mpoe_pt;
extern int mpoe_recv_pull(struct mpoe_iface * iface, struct mpoe_hdr * mh);
extern int mpoe_recv_pull_reply(struct mpoe_iface * iface, struct mpoe_hdr * mh);

/* pull */
extern int mpoe_init_pull(void);
extern void mpoe_exit_pull(void);

/* user regions */
extern void mpoe_endpoint_user_regions_init(struct mpoe_endpoint * endpoint);
extern int mpoe_register_user_region(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_deregister_user_region(struct mpoe_endpoint * endpoint, void __user * uparam);
extern void mpoe_endpoint_user_regions_exit(struct mpoe_endpoint * endpoint);

/* device */
extern int mpoe_dev_init(void);
extern void mpoe_dev_exit(void);

/* manage addresses */
static inline void
mpoe_mac_addr_of_netdevice(struct net_device * ifp,
			   struct mpoe_mac_addr * mpoe_addr)
{
	memcpy(mpoe_addr, ifp->dev_addr, sizeof(struct mpoe_mac_addr));
}

/* FIXME: assert sizes are equal */
static inline void
mpoe_ethhdr_src_to_mac_addr(struct mpoe_mac_addr * mpoe_addr,
			    struct ethhdr * eh)
{
	memcpy(mpoe_addr, eh->h_source, sizeof(eh->h_source));
}

static inline void
mpoe_mac_addr_to_ethhdr_dst(struct mpoe_mac_addr * mpoe_addr,
			    struct ethhdr * eh)
{
	memcpy(eh->h_dest, mpoe_addr, sizeof(eh->h_dest));
}

#endif /* __mpoe_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
