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

#include "omx_wire.h"

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

struct omx_iface;
struct omx_endpoint;
struct sk_buff;

/* globals */
extern struct omx_driver_desc * omx_driver_userdesc; /* exported read-only to user-space */

/* main net */
extern int omx_net_init(const char * ifnames);
extern void omx_net_exit(void);

/* dma if available */
extern int omx_dma_init(void);
extern void omx_dma_exit(void);

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
extern void omx_endpoint_pull_handles_prepare_exit(struct omx_endpoint * endpoint);
extern void omx_endpoint_pull_handles_force_exit(struct omx_endpoint * endpoint);

/* device */
extern int omx_dev_init(void);
extern void omx_dev_exit(void);

#endif /* __omx_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
