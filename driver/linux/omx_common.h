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
#include "omx_debug.h"

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
extern struct omx_driver_desc * omx_driver_userdesc; /* exported read-only to user-space */

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
extern void __omx_endpoint_last_release(struct kref *kref);
extern void omx_endpoints_cleanup(void);
extern int omx_endpoint_get_info(uint32_t board_index, uint32_t endpoint_index, uint32_t * closed, uint32_t * pid, char * command, size_t len);

static inline void
omx_endpoint_reacquire(struct omx_endpoint * endpoint)
{
	/* somebody must already hold a reference */
	kref_get(&endpoint->refcount);
}

static inline void
omx_endpoint_release(struct omx_endpoint * endpoint)
{
	kref_put(&endpoint->refcount, __omx_endpoint_last_release);
}

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
extern int omx_pull_handles_init(void);
extern void omx_pull_handles_exit(void);
extern int omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_pull_handles_prepare_exit(struct omx_endpoint * endpoint);

/* user regions */
extern void omx_endpoint_user_regions_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint);
extern int omx_user_region_register(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_user_region_deregister(struct omx_endpoint * endpoint, void __user * uparam);
extern struct omx_user_region * omx_user_region_acquire(struct omx_endpoint * endpoint, uint32_t rdma_id);
extern void __omx_user_region_last_release(struct kref * kref);

static inline void
omx_user_region_reacquire(struct omx_user_region * region)
{
	kref_get(&region->refcount);
}

static inline void
omx_user_region_release(struct omx_user_region * region)
{
	kref_put(&region->refcount, __omx_user_region_last_release);
}

extern void omx_user_region_release(struct omx_user_region * region);
extern int omx_user_region_append_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);
extern int omx_user_region_fill_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);

/* device */
extern int omx_dev_init(void);
extern void omx_dev_exit(void);

/* counters */
#define omx_counter_inc(iface, index)		\
do {						\
	iface->counters[OMX_COUNTER_##index]++;	\
} while (0)

/* misc */
extern int omx_cmd_bench(struct omx_endpoint * endpoint, void __user * uparam);

/* queue a skb for xmit, or eventually drop it */
#define __omx_queue_xmit(iface, skb, type)	\
do {						\
	omx_counter_inc(iface, SEND_##type);	\
	skb->dev = iface->eth_ifp;		\
	dev_queue_xmit(skb);			\
} while (0)

#ifdef OMX_DEBUG
#define omx_queue_xmit(iface, skb, type)					\
	do {									\
	if (omx_##type##_packet_loss &&						\
	    (++omx_##type##_packet_loss_index >= omx_##type##_packet_loss)) {	\
		kfree_skb(skb);							\
		omx_##type##_packet_loss_index = 0;				\
	} else {								\
		__omx_queue_xmit(iface, skb, type);				\
	}									\
} while (0)
#else /* OMX_DEBUG */
#define omx_queue_xmit __omx_queue_xmit
#endif /* OMX_DEBUG */

#endif /* __omx_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
