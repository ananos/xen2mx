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

/* constants */
#ifdef OMX_MX_WIRE_COMPAT
#define OMX_PULL_REPLY_LENGTH_MAX 4096UL
#define OMX_PULL_REPLY_PER_BLOCK 8
#else
#define OMX_PULL_REPLY_LENGTH_MAX 8192UL
#define OMX_PULL_REPLY_PER_BLOCK 31
#endif

#define OMX_MTU_MIN ((unsigned)(sizeof(struct omx_hdr)+max(OMX_PULL_REPLY_LENGTH_MAX,OMX_SENDQ_ENTRY_SIZE)))

#define OMX_IFNAMES_DEFAULT "all"

/* globals */
extern int omx_iface_max;
extern int omx_endpoint_max;
extern int omx_peer_max;
extern int omx_copybench;
extern struct omx_driver_desc * omx_driver_userdesc; /* exported read-only to user-space */

extern unsigned long omx_tiny_packet_loss;
extern unsigned long omx_small_packet_loss;
extern unsigned long omx_medium_packet_loss;
extern unsigned long omx_rndv_packet_loss;
extern unsigned long omx_pull_packet_loss;
extern unsigned long omx_pull_reply_packet_loss;
extern unsigned long omx_notify_packet_loss;
extern unsigned long omx_connect_packet_loss;
extern unsigned long omx_truc_packet_loss;
extern unsigned long omx_nack_lib_packet_loss;
extern unsigned long omx_nack_mcp_packet_loss;

/* main net */
extern int omx_net_init(const char * ifnames);
extern void omx_net_exit(void);

/* dma if available */
extern int omx_dma_init(void);
extern void omx_dma_exit(void);

/* manage endpoints */
extern int omx_iface_attach_endpoint(struct omx_endpoint * endpoint);
extern void omx_iface_detach_endpoint(struct omx_endpoint * endpoint, int ifacelocked);
extern int __omx_endpoint_close(struct omx_endpoint * endpoint, int ifacelocked);
extern struct omx_endpoint * omx_endpoint_acquire_by_iface_index(struct omx_iface * iface, uint8_t index);
extern void omx_endpoint_release(struct omx_endpoint * endpoint);
extern void omx_endpoints_cleanup(void);
extern int omx_endpoint_get_info(uint32_t board_index, uint32_t endpoint_index, uint32_t * closed, uint32_t * pid, char * command, size_t len);

static inline void omx_endpoint_reacquire(struct omx_endpoint * endpoint)
{ kref_get(&endpoint->refcount); } /* somebody must already hold a reference */

/* manage ifaces */
extern int omx_ifaces_show(char *buf);
extern int omx_ifaces_store(const char *buf, size_t size);
extern int omx_ifaces_get_count(void);
extern int omx_iface_get_id(uint8_t board_index, uint64_t * board_addr, char * hostname, char * ifacename);
extern struct omx_iface * omx_iface_find_by_ifp(struct net_device *ifp);
extern int omx_iface_get_counters(uint8_t board_index, int clear, uint64_t buffer_addr, uint32_t buffer_length);

/* manage peers */
extern int omx_peers_init(void);
extern void omx_peers_exit(void);
extern void omx_peers_clear(void);
extern int omx_peer_add(uint64_t board_addr, char *hostname);
extern int omx_peer_set_reverse_index(uint16_t index, uint16_t reverse_index);
extern int omx_set_target_peer(struct omx_hdr *mh, uint16_t index);
extern int omx_check_recv_peer_index(uint16_t peer_index);
extern int omx_peer_lookup_by_index(uint32_t index, uint64_t *board_addr, char *hostname);
extern int omx_peer_lookup_by_addr(uint64_t board_addr, char *hostname, uint32_t *index);
extern int omx_peer_lookup_by_hostname(char *hostname, uint64_t *board_addr, uint32_t *index);

/* events */
extern void omx_endpoint_queues_init(struct omx_endpoint *endpoint);
extern int omx_notify_exp_event(struct omx_endpoint *endpoint, uint8_t type, void *event, int length);
extern int omx_notify_unexp_event(struct omx_endpoint *endpoint, uint8_t type, void *event, int length);
extern int omx_prepare_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint, unsigned long *recvq_offset);
extern void omx_commit_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint, uint8_t type, void *event, int length);
extern int omx_wait_event(struct omx_endpoint * endpoint, void __user * uparam);

/* sending */
extern struct sk_buff * omx_new_skb(unsigned long len);
extern int omx_send_tiny(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_small(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_medium(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_rndv(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_pull(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_notify(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_connect(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_send_truc(struct omx_endpoint * endpoint, void __user * uparam);
extern void omx_send_nack_lib(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type, uint8_t src_endpoint, uint8_t dst_endpoint, uint16_t lib_seqnum);
extern void omx_send_nack_mcp(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type, uint8_t src_endpoint, uint32_t src_pull_handle, uint32_t src_magic);

/* receiving */
extern void omx_pkt_type_handlers_init(void);
extern struct packet_type omx_pt;
extern int omx_recv_pull(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_pull_reply(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_nack_mcp(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);

/* pull */
extern int omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_pull_handles_exit(struct omx_endpoint * endpoint);
extern void omx_pull_handles_cleanup(void);

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

/* counters */
static inline void omx_counter_inc(struct omx_iface * iface, enum omx_counter_index index) { iface->counters[index]++; }

/* misc */
extern int omx_cmd_bench(struct omx_endpoint * endpoint, void __user * uparam);

/* queue a skb for xmit, or eventually drop it */
#ifdef OMX_DEBUG
#define omx_queue_xmit(iface, skb, type)					\
	do {									\
	if (omx_##type##_packet_loss &&						\
	    (++omx_##type##_packet_loss_index >= omx_##type##_packet_loss)) {	\
		kfree_skb(skb);							\
		omx_##type##_packet_loss_index = 0;				\
	} else {								\
		skb->dev = iface->eth_ifp;					\
		dev_queue_xmit(skb);						\
	}									\
} while (0)

#else /* OMX_DEBUG */
#define omx_queue_xmit(iface, skb, type) dev_queue_xmit(skb);
#endif /* OMX_DEBUG */

/* translate omx_endpoint_acquire_by_iface_index return values into nack type */
static inline enum omx_nack_type
omx_endpoint_acquire_by_iface_index_error_to_nack_type(void * errptr)
{
	switch (PTR_ERR(errptr)) {
	case -EINVAL:
		return OMX_NACK_TYPE_BAD_ENDPT;
	case -ENOENT:
		return OMX_NACK_TYPE_ENDPT_CLOSED;
	}

	BUG();
	return 0; /* shut-up the compiler */
}

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

#define OMX_DEBUG_SEND (1<<0)
#define OMX_DEBUG_RECV (1<<1)
#define OMX_DEBUG_DROP (1<<2)
#define OMX_DEBUG_PULL (1<<3)
#define OMX_DEBUG_REG (1<<4)
#define OMX_DEBUG_IOCTL (1<<5)
#define OMX_DEBUG_EVENT (1<<6)
#define OMX_DEBUG_PEER (1<<7)

extern unsigned long omx_debug;
#define omx_debug_type_enabled(type) (OMX_DEBUG_##type & omx_debug)

#define dprintk(type, x...) do { if (omx_debug_type_enabled(type)) printk(KERN_INFO x); } while (0)

#else /* OMX_DEBUG */
#define dprintk(type, x...) do { /* nothing */ } while (0)
#endif /* OMX_DEBUG */

#define omx_send_dprintk(_eh, _format, ...) \
dprintk(SEND, \
	"Open-MX: sending from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__)

#define omx_recv_dprintk(_eh, _format, ...) \
dprintk(RECV, \
	"Open-MX: received from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__);

#define omx_drop_dprintk(_eh, _format, ...) \
dprintk(DROP, \
	"Open-MX: dropping pkt from %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
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
