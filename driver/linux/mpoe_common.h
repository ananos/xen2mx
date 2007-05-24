#ifndef __mpoe_common_h__
#define __mpoe_common_h__

#include <linux/fs.h>

#include "mpoe_io.h"

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

extern int mpoe_net_attach_endpoint(struct mpoe_endpoint * endpoint, uint8_t board_index, uint8_t endpoint_index);
extern void mpoe_net_detach_endpoint(struct mpoe_endpoint * endpoint);
extern int mpoe_close_endpoint(struct mpoe_endpoint * endpoint, void __user * dummy);

extern int mpoe_net_ifaces_show(char *buf);
extern int mpoe_net_ifaces_store(const char *buf, size_t size);

extern int mpoe_net_get_iface_count(void);
extern int mpoe_net_get_iface_id(uint8_t board_index, uint64_t * board_id);

extern int mpoe_net_send_tiny(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_net_send_medium(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_net_send_rendez_vous(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_net_send_pull(struct mpoe_endpoint * endpoint, void __user * uparam);

extern void mpoe_init_endpoint_user_regions(struct mpoe_endpoint * endpoint);
extern int mpoe_register_user_region(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_deregister_user_region(struct mpoe_endpoint * endpoint, void __user * uparam);
extern void mpoe_deregister_endpoint_user_regions(struct mpoe_endpoint * endpoint);

extern int mpoe_dev_init(void);
extern void mpoe_dev_exit(void);

#endif /* __mpoe_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
