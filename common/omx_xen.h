/*
 * Xen2MX
 * Copyright © Anastassios Nanos 2012
 * (see AUTHORS file)
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

#ifndef __omx_xen_h__
#define __omx_xen_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_io.h"
#include "omx_wire.h"

#define OMX_XEN_MAX_ENDPOINTS OMX_ENDPOINT_INDEX_MAX
#define OMX_XEN_GRANT_PAGES_MAX 16

/* FIXME: Don't miss this one!!!,
 * allocated memory in the backend rises exponentially  */
#define OMX_XEN_COOKIES 1

struct omx_cmd_xen_send_mediumsq_frag_done {
	struct omx_evt_send_mediumsq_frag_done sq_frag_done;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_mediumsq_frag {
	struct omx_cmd_send_mediumsq_frag mediumsq_frag;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_mediumva {
	uint8_t nr_pages;
	uint16_t first_page_offset;
	struct omx_cmd_send_mediumva mediumva;
	grant_ref_t grefs[9];
} __attribute__ ((__packed__));

struct omx_cmd_xen_pull {
	struct omx_cmd_pull pull;
} __attribute__ ((__packed__));

struct omx_cmd_xen_recv_pull_request {
	struct omx_evt_recv_pull_request {
		uint8_t dst_endpoint;
		uint8_t src_endpoint;
		uint32_t session_id;
		uint32_t block_length;
		uint32_t first_frame_offset;
		uint32_t pulled_rdma_id;
		uint32_t pulled_rdma_offset;

		uint32_t src_pull_handle;
		uint32_t src_magic;
		uint32_t frame_index;
		uint16_t peer_index;
	} pull_req;
	uint32_t rid;
} __attribute__ ((__packed__));

struct omx_cmd_xen_recv_pull_done {
	struct omx_evt_pull_done pull_done;
	uint32_t rid;
} __attribute__ ((__packed__));

struct omx_cmd_xen_recv_connect_request {
	struct omx_evt_recv_connect_request request;
} __attribute__ ((__packed__));

struct omx_cmd_xen_recv_connect_reply {
	struct omx_evt_recv_connect_reply reply;
} __attribute__ ((__packed__));

struct omx_cmd_xen_recv_liback {
	struct omx_evt_recv_liback liback;
} __attribute__ ((__packed__));

struct omx_cmd_xen_recv_msg {
	struct omx_evt_recv_msg msg;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_rndv {
	struct omx_cmd_send_rndv rndv;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_small {
	struct omx_cmd_send_small small;
	char data[OMX_SMALL_MSG_LENGTH_MAX];
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_tiny {
	struct omx_cmd_send_tiny tiny;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_connect_request {
	struct omx_cmd_send_connect_request request;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_connect_reply {
	struct omx_cmd_send_connect_reply reply;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_notify {
	struct omx_cmd_send_notify notify;
} __attribute__ ((__packed__));

struct omx_cmd_xen_send_liback {
	struct omx_cmd_send_liback liback;
} __attribute__ ((__packed__));

struct omx_cmd_xen_get_board_info {
	struct omx_board_info info;
} __attribute__ ((__packed__));

struct omx_cmd_xen_get_endpoint_info {
	struct omx_endpoint_info info;
} __attribute__ ((__packed__));

struct omx_cmd_xen_get_counters {
	uint8_t clear;
	uint8_t pad1[3];
	uint64_t buffer_addr;
	uint32_t buffer_length;
	int ret;
} __attribute__ ((__packed__));

struct omx_cmd_xen_get_board_count {
	uint32_t board_count;
} __attribute__ ((__packed__));

struct omx_cmd_xen_set_hostname {
	char hostname[OMX_HOSTNAMELEN_MAX];
} __attribute__ ((__packed__));

struct omx_cmd_xen_peer_table_state {
	struct omx_cmd_peer_table_state state;
} __attribute__ ((__packed__));

struct omx_cmd_xen_misc_peer_info {
	struct omx_cmd_misc_peer_info info;
} __attribute__ ((__packed__));

struct omx_cmd_xen_bench {
	struct omx_cmd_bench_hdr hdr;
	/* 8 */
	uint32_t pad;
	char dummy_data[OMX_TINY_MSG_LENGTH_MAX];
	/* 40 */
} __attribute__ ((__packed__));

struct omx_ring_msg_register_user_segment {
	uint32_t rid;
	uint32_t eid;
	/* 8 */
	uint32_t aligned_vaddr;
	uint16_t first_page_offset;
	int16_t status;
	/* 16 */
	uint32_t length;
	uint32_t nr_pages;
	/* 24 */
	uint32_t nr_grefs;
	uint32_t gref[OMX_XEN_GRANT_PAGES_MAX];
	uint32_t sid;
	/* 32 */
	uint16_t gref_offset;
	uint8_t nr_parts;
} __attribute__ ((__packed__));

struct omx_ring_msg_deregister_user_segment {
	uint32_t rid;
	uint32_t eid;
	/* 8 */
	uint32_t aligned_vaddr;
	uint16_t first_page_offset;
	int16_t status;
	/* 16 */
	uint32_t length;
	uint32_t nr_pages;
	/* 24 */
	uint32_t nr_grefs;
	uint32_t gref[OMX_XEN_GRANT_PAGES_MAX];
	uint32_t sid;
	/* 32 */
	uint16_t gref_offset;
	uint8_t nr_parts;
} __attribute__ ((__packed__));

struct omx_ring_msg_create_user_region {
	uint32_t id;
	uint32_t nr_segments;
	/* 8 */
	uint32_t seqnum;
	uint16_t offset;
	uint8_t eid;
	uint8_t status;
	/* 16 */
	uint64_t vaddr;
	/* 24 */
	uint32_t nr_grefs;
	uint32_t nr_pages;
	/* 32 */
	struct omx_ring_msg_register_user_segment segs[2];
} __attribute__ ((__packed__));

struct omx_ring_msg_destroy_user_region {
	uint32_t id;
	uint32_t seqnum;
	/* 8 */
	uint8_t eid;
	uint8_t status;
	uint16_t nr_segments;
	/* 16 */
	uint64_t region;
	/* 24 */
	uint64_t pad2;
	/* 32 */
	struct omx_ring_msg_deregister_user_segment segs[2];
} __attribute__ ((__packed__));

struct omx_ring_msg_endpoint {
	struct omx_endpoint *endpoint;
	uint32_t session_id;
	uint32_t sendq_gref_size;
	uint32_t recvq_gref_size;
	uint16_t egref_sendq_offset;
	uint16_t egref_recvq_offset;
	grant_ref_t sendq_gref;
	grant_ref_t recvq_gref;
	grant_ref_t endpoint_gref;
	uint16_t endpoint_offset;
} __attribute__ ((__packed__));

struct omx_xenif_request {
	uint32_t func;
	uint32_t board_index;
	uint32_t eid;
	int ret;
	union {
		struct omx_ring_msg_register_user_segment cus;
		struct omx_ring_msg_deregister_user_segment dus;
		struct omx_ring_msg_create_user_region cur;
		struct omx_ring_msg_destroy_user_region dur;
		struct omx_ring_msg_endpoint endpoint;
		struct omx_cmd_xen_get_board_info gbi;
		struct omx_cmd_xen_get_endpoint_info gei;
		struct omx_cmd_xen_get_counters gc;
		struct omx_cmd_xen_set_hostname sh;
		struct omx_cmd_xen_misc_peer_info mpi;
		struct omx_cmd_xen_bench cxb;
		struct omx_cmd_xen_get_board_count gbc;
		struct omx_cmd_xen_peer_table_state pts;
		struct omx_cmd_xen_send_connect_request send_connect_request;
		struct omx_cmd_xen_send_connect_reply send_connect_reply;
		struct omx_cmd_xen_send_notify send_notify;
		struct omx_cmd_xen_send_liback send_liback;
		struct omx_cmd_xen_send_rndv send_rndv;
		struct omx_cmd_xen_recv_connect_request recv_connect_request;
		struct omx_cmd_xen_recv_connect_reply recv_connect_reply;
		struct omx_cmd_xen_recv_msg recv_msg;
		struct omx_cmd_xen_recv_pull_request recv_pull_request;
		struct omx_cmd_xen_recv_pull_done recv_pull_done;
		struct omx_cmd_xen_recv_liback recv_liback;
		struct omx_cmd_xen_send_tiny send_tiny;
		struct omx_cmd_xen_send_small send_small;
		struct omx_cmd_xen_send_mediumsq_frag send_mediumsq_frag;
		struct omx_cmd_xen_send_mediumsq_frag_done
		    send_mediumsq_frag_done;
		struct omx_cmd_xen_send_mediumva send_mediumva;
		struct omx_cmd_xen_pull pull;
	} data;
} __attribute__ ((__packed__));

struct omx_xenif_response {
	uint32_t func;
	uint32_t board_index;
	uint32_t eid;
	int ret;
	union {
		struct omx_ring_msg_register_user_segment cus;
		struct omx_ring_msg_deregister_user_segment dus;
		struct omx_ring_msg_create_user_region cur;
		struct omx_ring_msg_destroy_user_region dur;
		struct omx_ring_msg_endpoint endpoint;
		struct omx_cmd_xen_get_board_info gbi;
		struct omx_cmd_xen_get_endpoint_info gei;
		struct omx_cmd_xen_get_counters gc;
		struct omx_cmd_xen_set_hostname sh;
		struct omx_cmd_xen_misc_peer_info mpi;
		struct omx_cmd_xen_bench cxb;
		struct omx_cmd_xen_get_board_count gbc;
		struct omx_cmd_xen_peer_table_state pts;
		struct omx_cmd_xen_send_connect_request send_connect_request;
		struct omx_cmd_xen_send_connect_reply send_connect_reply;
		struct omx_cmd_xen_send_notify send_notify;
		struct omx_cmd_xen_send_liback send_liback;
		struct omx_cmd_xen_send_rndv send_rndv;
		struct omx_cmd_xen_recv_connect_request recv_connect_request;
		struct omx_cmd_xen_recv_connect_reply recv_connect_reply;
		struct omx_cmd_xen_recv_msg recv_msg;
		struct omx_cmd_xen_recv_pull_request recv_pull_request;
		struct omx_cmd_xen_recv_pull_done recv_pull_done;
		struct omx_cmd_xen_recv_liback recv_liback;
		struct omx_cmd_xen_send_tiny send_tiny;
		struct omx_cmd_xen_send_small send_small;
		struct omx_cmd_xen_send_mediumsq_frag send_mediumsq_frag;
		struct omx_cmd_xen_send_mediumsq_frag_done
		    send_mediumsq_frag_done;
		struct omx_cmd_xen_send_mediumva send_mediumva;
		struct omx_cmd_xen_pull pull;
	} data;
} __attribute__ ((__packed__));

DEFINE_RING_TYPES(omx_xenif, struct omx_xenif_request,
		  struct omx_xenif_response);

enum omx_xenif_state {
	OMXIF_STATE_DISCONNECTED,
	OMXIF_STATE_CONNECTED,
	OMXIF_STATE_SUSPENDED,
};

#endif				/* __omx_xen_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
