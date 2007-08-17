/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#ifndef __omx_common_h__
#define __omx_common_h__

#include "omx_types.h"

/* globals */
extern int omx_iface_max;
extern int omx_endpoint_max;
extern int omx_peer_max;
extern int omx_copybench;

/* main net */
extern int omx_net_init(const char * ifnames);
extern void omx_net_exit(void);

/* manage endpoints */
extern int omx_iface_attach_endpoint(struct omx_endpoint * endpoint);
extern void omx_iface_detach_endpoint(struct omx_endpoint * endpoint, int ifacelocked);
extern int __omx_endpoint_close(struct omx_endpoint * endpoint, int ifacelocked);
extern int omx_endpoint_acquire(struct omx_endpoint * endpoint);
extern struct omx_endpoint * omx_endpoint_acquire_by_iface_index(struct omx_iface * iface, uint8_t index);
extern union omx_evt * omx_find_next_eventq_slot(struct omx_endpoint *endpoint);
extern void omx_endpoint_release(struct omx_endpoint * endpoint);

/* manage ifaces */
extern int omx_ifaces_show(char *buf);
extern int omx_ifaces_store(const char *buf, size_t size);
extern int omx_ifaces_get_count(void);
extern int omx_iface_get_id(uint8_t board_index, uint64_t * board_addr, char * board_name);
extern struct omx_iface * omx_iface_find_by_ifp(struct net_device *ifp);

/* sending */
extern struct sk_buff * omx_new_skb(struct net_device *ifp, unsigned long len);
extern int omx_send_tiny(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_small(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_medium(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_rndv(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_pull(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_notify(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_connect(struct omx_endpoint * endpoint, void __user * uparam);

/* receiving */
extern void omx_pkt_type_handlers_init(void);
extern struct packet_type omx_pt;
extern int omx_recv_pull(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_pull_reply(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);

/* pull */
extern int omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_pull_handles_exit(struct omx_endpoint * endpoint);

/* user regions */
extern void omx_endpoint_user_regions_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint);
extern int omx_user_region_register(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_user_region_deregister(struct omx_endpoint * endpoint, void __user * uparam);
extern struct omx_user_region * omx_user_region_acquire(struct omx_endpoint * endpoint, uint32_t rdma_id);

static inline void omx_user_region_reacquire(struct omx_user_region * region)
{ atomic_inc(&region->refcount); } /* somebody must already hold a reference */

extern void omx_user_region_release(struct omx_user_region * region);
extern int omx_user_region_append_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);
extern int omx_user_region_fill_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);

/* device */
extern int omx_dev_init(void);
extern void omx_dev_exit(void);

/* misc */
extern int omx_cmd_bench(struct omx_endpoint * endpoint, void __user * uparam);

/* manage addresses */
static inline uint64_t
omx_board_addr_from_netdevice(struct net_device * ifp)
{
	return (((uint64_t) ifp->dev_addr[0]) << 40)
	     + (((uint64_t) ifp->dev_addr[1]) << 32)
	     + (((uint64_t) ifp->dev_addr[2]) << 24)
	     + (((uint64_t) ifp->dev_addr[3]) << 16)
	     + (((uint64_t) ifp->dev_addr[4]) << 8)
	     + (((uint64_t) ifp->dev_addr[5]) << 0);
}

static inline uint64_t
omx_board_addr_from_ethhdr_src(struct ethhdr * eh)
{
	return (((uint64_t) eh->h_source[0]) << 40)
	     + (((uint64_t) eh->h_source[1]) << 32)
	     + (((uint64_t) eh->h_source[2]) << 24)
	     + (((uint64_t) eh->h_source[3]) << 16)
	     + (((uint64_t) eh->h_source[4]) << 8)
	     + (((uint64_t) eh->h_source[5]) << 0);
}

static inline void
omx_board_addr_to_ethhdr_dst(struct ethhdr * eh, uint64_t board_addr)
{
	eh->h_dest[0] = (uint8_t)(board_addr >> 40);
	eh->h_dest[1] = (uint8_t)(board_addr >> 32);
	eh->h_dest[2] = (uint8_t)(board_addr >> 24);
	eh->h_dest[3] = (uint8_t)(board_addr >> 16);
	eh->h_dest[4] = (uint8_t)(board_addr >> 8);
	eh->h_dest[5] = (uint8_t)(board_addr >> 0);
}

#ifdef OMX_DEBUG
#define dprintk(x...) printk(KERN_INFO x)
#else
#define dprintk(x...) do { /* nothing */ } while (0)
#endif

#define omx_send_dprintk(_eh, _format, ...) \
dprintk("Open-MX: sending from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__)

#define omx_recv_dprintk(_eh, _format, ...) \
dprintk("Open-MX: received from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__);

#define omx_drop_dprintk(_eh, _format, ...) \
dprintk("Open-MX: dropping pkt from %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	##__VA_ARGS__);

#endif /* __omx_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
