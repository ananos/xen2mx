/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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
#include "omx_io.h"

struct omx_iface;
struct omx_iface_raw;
struct omx_endpoint;
struct sk_buff;

/* constants */
#define OMX_PULL_BLOCK_DESCS_NR 4
#define OMX_IFACE_RX_USECS_WARN_MIN 10

/* globals */
extern struct omx_driver_desc * omx_driver_userdesc; /* exported read-only to user-space */

/* defined as module parameters */
extern int omx_iface_max;
extern int omx_endpoint_max;
extern int omx_peer_max;
extern int omx_skb_frags;
extern int omx_skb_copy_max;
extern int omx_copybench;
extern int omx_pin_synchronous;
extern int omx_pin_progressive;
extern int omx_pin_chunk_pages_min;
extern int omx_pin_chunk_pages_max;
extern int omx_pin_invalidate;
extern unsigned long omx_user_rights;

/* events */
extern void omx_endpoint_queues_init(struct omx_endpoint *endpoint);
extern int omx_notify_exp_event(struct omx_endpoint *endpoint, uint8_t type, void *event, int length);
extern int omx_notify_unexp_event(struct omx_endpoint *endpoint, uint8_t type, void *event, int length);
extern int omx_prepare_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint, unsigned long *recvq_offset);
extern int omx_prepare_notify_unexp_events_with_recvq(struct omx_endpoint *endpoint, int nr, unsigned long *recvq_offset);
extern void omx_commit_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint, uint8_t type, void *event, int length);
extern void omx_cancel_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint);
extern int omx_ioctl_wait_event(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_wakeup(struct omx_endpoint * endpoint, void __user * uparam);
extern void omx_wakeup_endpoint_on_close(struct omx_endpoint * endpoint);

/* sending */
extern struct sk_buff * omx_new_skb(unsigned long len);
extern int omx_ioctl_send_tiny(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_small(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_mediumsq_frag(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_mediumva(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_rndv(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_pull(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_notify(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_connect(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_send_truc(struct omx_endpoint * endpoint, void __user * uparam);
extern void omx_send_nack_lib(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type, uint8_t src_endpoint, uint8_t dst_endpoint, uint16_t lib_seqnum);
extern void omx_send_nack_mcp(struct omx_iface * iface, uint32_t peer_index, enum omx_nack_type nack_type, uint8_t src_endpoint, uint32_t src_pull_handle, uint32_t src_magic);

/* receiving */
extern void omx_pkt_types_init(void);
extern struct packet_type omx_pt;
extern int omx_recv_pull_request(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_pull_reply(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_nack_mcp(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);

/* pull */
extern int omx_endpoint_pull_handles_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_pull_handles_exit(struct omx_endpoint * endpoint);

/* device */
extern int omx_dev_init(void);
extern void omx_dev_exit(void);

/* raw */
extern int omx_raw_init(void);
extern void omx_raw_exit(void);
extern void omx_iface_raw_init(struct omx_iface_raw *raw);
extern void omx_iface_raw_exit(struct omx_iface_raw *raw);
extern int omx_recv_raw(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_host_query(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);
extern int omx_recv_host_reply(struct omx_iface * iface, struct omx_hdr * mh, struct sk_buff * skb);

/* misc */
extern char * omx_get_driver_string(unsigned int *lenp);

/* user rights */
#define OMX_USER_RIGHT_COUNTERS (1<<0)
#define OMX_USER_RIGHT_HOSTNAME (1<<1)
#define OMX_USER_RIGHT_PEERTABLE (1<<2)
#define OMX_HAS_USER_RIGHT(x) ((omx_user_rights & OMX_USER_RIGHT_##x) || capable(CAP_SYS_ADMIN))

#endif /* __omx_common_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
