#ifndef __mpoe_common_h__
#define __mpoe_common_h__

#include "mpoe_types.h"

/* globals */
extern int mpoe_iface_max;
extern int mpoe_endpoint_max;

/* main net */
extern int mpoe_net_init(const char * ifnames);
extern void mpoe_net_exit(void);

/* manage endpoints */
extern int mpoe_iface_attach_endpoint(struct mpoe_endpoint * endpoint);
extern void mpoe_iface_detach_endpoint(struct mpoe_endpoint * endpoint, int ifacelocked);
extern int __mpoe_endpoint_close(struct mpoe_endpoint * endpoint, int ifacelocked);
extern int mpoe_endpoint_acquire(struct mpoe_endpoint * endpoint);
extern struct mpoe_endpoint * mpoe_endpoint_acquire_by_iface_index(struct mpoe_iface * iface, uint8_t index);
extern union mpoe_evt * mpoe_find_next_eventq_slot(struct mpoe_endpoint *endpoint);
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
extern int mpoe_send_small(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_send_medium(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_send_rendez_vous(struct mpoe_endpoint * endpoint, void __user * uparam);
extern int mpoe_send_pull(struct mpoe_endpoint * endpoint, void __user * uparam);

/* receiving */
extern void mpoe_pkt_type_handlers_init(void);
extern struct packet_type mpoe_pt;
extern int mpoe_recv_pull(struct mpoe_iface * iface, struct mpoe_hdr * mh, struct sk_buff * skb);
extern int mpoe_recv_pull_reply(struct mpoe_iface * iface, struct mpoe_hdr * mh, struct sk_buff * skb);

/* pull */
extern int mpoe_endpoint_pull_handles_init(struct mpoe_endpoint * endpoint);
extern void mpoe_endpoint_pull_handles_exit(struct mpoe_endpoint * endpoint);

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

#ifdef MPOE_DEBUG
#define dprintk(x...) printk(KERN_INFO x)
#else
#define dprintk(x...) do { /* nothing */ } while (0)
#endif

#define mpoe_send_dprintk(_eh, _format, ...) \
dprintk("MPoE: sending from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__)

#define mpoe_recv_dprintk(_eh, _format, ...) \
dprintk("MPoE: received from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__);

#endif /* __mpoe_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
