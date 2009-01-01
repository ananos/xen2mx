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

#ifndef __omx_peer_h__
#define __omx_peer_h__

#include <linux/rcupdate.h>

struct omx_iface;
struct omx_pkt_head;
struct omx_peer;

extern int omx_peers_init(void);
extern void omx_peers_exit(void);
extern void omx_peers_clear(int local);
extern void omx_peers_clear_names(void);
extern int omx_peers_notify_iface_attach(struct omx_iface * iface);
extern void omx_peers_notify_iface_detach(struct omx_iface * iface);
extern int omx_peer_add(uint64_t board_addr, char *hostname);
extern void omx_peer_set_reverse_index_locked(struct omx_peer *peer, uint16_t reverse_index);
extern struct omx_endpoint * omx_local_peer_acquire_endpoint(uint16_t peer_index, uint8_t endpoint_index);
extern int omx_set_target_peer(struct omx_pkt_head *ph, uint16_t index);
extern int omx_check_recv_peer_index(uint16_t peer_index);

extern int omx_peer_lookup_by_index(uint32_t index, uint64_t *board_addr, char *hostname);
extern int omx_peer_lookup_by_addr(uint64_t board_addr, char *hostname, uint32_t *index);
extern int omx_peer_lookup_by_hostname(char *hostname, uint64_t *board_addr, uint32_t *index);
extern struct omx_peer * omx_peer_lookup_by_addr_locked(uint64_t board_addr);

extern void omx_process_host_queries_and_replies(void);
extern void omx_process_peers_to_host_query(void);

#define OMX_UNKNOWN_REVERSE_PEER_INDEX ((uint32_t)-1)

struct omx_peer {
	uint64_t board_addr;
	char *hostname;
	uint32_t index; /* this peer index in our table */
	uint32_t reverse_index; /* our index in this peer table, or OMX_UNKNOWN_REVERSE_PEER_INDEX */
	struct list_head addr_hash_elt;
	struct omx_iface * local_iface;

	struct list_head host_query_list_elt;
	uint64_t host_query_last_resend_jiffies;

	struct rcu_head rcu_head; /* rcu deferred free callback */
};

#endif /* __omx_peer_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
