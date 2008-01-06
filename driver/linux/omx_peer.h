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

#ifndef __omx_peer_h__
#define __omx_peer_h__

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

struct omx_peer {
	uint64_t board_addr;
	char *hostname;
	uint32_t index; /* this peer index in our table */
	uint32_t reverse_index; /* our index in this peer table, or OMX_UNKNOWN_REVERSE_PEER_INDEX */
	struct list_head addr_hash_elt;
};

#endif /* __omx_peer_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
