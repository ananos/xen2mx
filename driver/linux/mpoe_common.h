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

enum mpoe_iface_status {
	/* iface is ready to be used */
	MPOE_IFACE_STATUS_OK,
	/* iface is being closed by somebody else, no new endpoint may be open */
	MPOE_IFACE_STATUS_CLOSING,
};

struct mpoe_iface {
	int index;

	struct net_device * eth_ifp;

	spinlock_t endpoint_lock;
	int endpoint_nr;
	struct mpoe_endpoint ** endpoints;

	enum mpoe_iface_status status;
};

enum mpoe_endpoint_status {
	/* endpoint is free and may be open */
	MPOE_ENDPOINT_STATUS_FREE,
	/* endpoint is already being open by somebody else */
	MPOE_ENDPOINT_STATUS_INITIALIZING,
	/* endpoint is ready to be used */
	MPOE_ENDPOINT_STATUS_OK,
	/* endpoint is being closed by somebody else */
	MPOE_ENDPOINT_STATUS_CLOSING,
};

struct mpoe_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;

	spinlock_t lock;
	enum mpoe_endpoint_status status;
	atomic_t refcount;
	wait_queue_head_t noref_queue;

	struct mpoe_iface * iface;

	void * sendq, * recvq, * eventq;
	union mpoe_evt * next_eventq_slot;
	char * next_recvq_slot;

	spinlock_t user_regions_lock;
	struct mpoe_user_region * user_regions[MPOE_USER_REGION_MAX];
};


/************************
 * Notes about locking:
 *
 * The endpoint has 2 main status: FREE and OK. To prevent 2 people from changing it
 * at the same time, it is protected by a lock. To reduce the time we hold the lock,
 * there are 2 intermediate status: INITIALIZING and CLOSING.
 * When an endpoint is being used, its refcount is increased (by acquire/release)
 * When somebody wants to close an endpoint, it sets the CLOSING status (so that
 * new users can't acquire the endpoint) and waits for current users to release
 * (when refcount becomes 0).
 *
 * The iface doesn't have an actual refcount since it has a number of endpoints attached.
 * There's a lock to protect this array against concurrent endpoint attach/detach.
 * When removing an iface (either by the user or by the netdevice notifier), the status
 * is set to CLOSING so that any new endpoint opener fails.
 *
 * The list of ifaces is always coherent since new ifaces are only added once initialized,
 * and removed in a coherent state (endpoints have been properly detached first)
 * Incoming packet processing is disabled while removing an iface.
 * So scanning the array of ifaces does not require locking,
 * but looking in the iface internals requires locking.
 * The iface may not be removed while processing an incoming packet, so
 * we don't need locking and no need hold a reference on the iface either.
 *
 * The locks are always taken in this priority order:
 * mpoe_iface_lock, iface->endpoint_lock, endpoint->lock
 */


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
extern int mpoe_iface_attach_endpoint(struct mpoe_endpoint * endpoint);
extern void mpoe_iface_detach_endpoint(struct mpoe_endpoint * endpoint, int ifacelocked);
extern int __mpoe_endpoint_close(struct mpoe_endpoint * endpoint, int ifacelocked);
extern struct mpoe_endpoint * mpoe_endpoint_acquire_by_iface_index(struct mpoe_iface * iface, uint8_t index);
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
