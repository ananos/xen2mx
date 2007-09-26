/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#ifndef __omx_types_h__
#define __omx_types_h__

#include <stdint.h>

#include "omx_io.h"
#include "omx_list.h"

/*****************
 * Internal types
 */

struct omx__sendq_map {
  int first_free;
  int nr_free;
  struct omx__sendq_entry {
    int next_free;
    void * user;
  } * array;
};

struct omx__large_region_map {
  int first_free;
  int nr_free;
  struct omx__large_region_slot {
    int next_free;
    struct omx__large_region {
      uint8_t id;
      uint8_t seqnum;
      uint16_t offset;
      void * user;
    } region;
  } * array;
};

typedef uint16_t omx__seqnum_t; /* FIXME: assert same size on the wire */

struct omx__partner {
  uint64_t board_addr;
  uint16_t peer_index;
  uint8_t endpoint_index;
  uint32_t session_id;

  /* seq num of the last connect request to this partner */
  uint8_t connect_seqnum;

  /* index of our peer in the table of the partner */
  uint16_t dest_src_peer_index;

  /* list of request matched but not entirely received */
  struct list_head partialq;

  /* early packets */
  struct list_head earlyq;

  /* seqnum of the next send */
  omx__seqnum_t next_send_seq;

  /* seqnum of the next entire message to match
   * used to know to accumulate/match/defer a fragment
   */
  omx__seqnum_t next_match_recv_seq;

  /* seqnum of the next fragment to recv
   * next_frag_recv_seq < next_match_recv_seq in case of partially received medium
   * used to ack back to the partner
   * (all seqnum < next_frag_recv_seq have been entirely received)
   */
  omx__seqnum_t next_frag_recv_seq;

  /*
   * when matching, increase recv_seq
   * when event, compare message seqnum with next_match_recv_seq:
   * - if == , matching
   * - if < , find partial receive in partner's queue
   * - if < , queue as a early fragment
   *
   * when completing an event, recompute next_frag_recv_seq
   * - if partial receive (ordered), use its seqnum
   * - if no partial receive, use next_match_recv_seq
   * if changing next_frag_recv_seq, ack all the previous seqnums
   */

  void * user_context;
};

#define CTXID_FROM_MATCHING(ep, match) ((uint32_t)(((match) >> (ep)->ctxid_shift) & ((ep)->ctxid_max-1)))
#define CHECK_MATCHING_WITH_CTXID(ep, match) (((match) & (ep)->ctxid_mask) == (ep)->ctxid_mask)

struct omx_endpoint {
  int fd;
  int endpoint_index, board_index;
  char hostname[OMX_HOSTNAMELEN_MAX];
  char ifacename[OMX_IF_NAMESIZE];
  uint32_t session_id;
  uint32_t app_key;
  omx_unexp_handler_t unexp_handler;
  void * unexp_handler_context;
  int in_handler;
  void * recvq, * sendq, * exp_eventq, * unexp_eventq;
  void * next_exp_event, * next_unexp_event;
  uint32_t avail_exp_events;

  /* context ids */
  uint8_t ctxid_bits;
  uint32_t ctxid_max;
  uint8_t ctxid_shift;
  uint64_t ctxid_mask;

  /* context id array for multiplexed queues */
  struct {
    struct list_head unexp_req_q;
    struct list_head recv_req_q;
    struct list_head done_req_q;
  } * ctxid;

  /* non multiplexed queues */
  struct list_head queued_send_req_q; /* SEND req with state = QUEUED */
  struct list_head sent_req_q; /* SEND req with state = IN_DRIVER */
  struct list_head multifrag_medium_recv_req_q; /* RECV req with state = PARTIAL */
  struct list_head large_send_req_q; /* SEND req with state = NEED_REPLY */
  struct list_head pull_req_q; /* RECV_LARGE req with state = IN_DRIVER */
  struct list_head connect_req_q; /* CONNECT req with state = NEED_REPLY */

  struct omx__sendq_map sendq_map;
  struct omx__large_region_map large_region_map;
  struct omx__partner ** partners;
  struct omx__partner * myself;
};

enum omx__request_type {
  OMX_REQUEST_TYPE_NONE=0,
  OMX_REQUEST_TYPE_CONNECT,
  OMX_REQUEST_TYPE_SEND_TINY,
  OMX_REQUEST_TYPE_SEND_SMALL,
  OMX_REQUEST_TYPE_SEND_MEDIUM,
  OMX_REQUEST_TYPE_SEND_LARGE,
  OMX_REQUEST_TYPE_RECV,
  OMX_REQUEST_TYPE_RECV_LARGE,
};

enum omx__request_state {
  OMX_REQUEST_STATE_DONE = (1<<0),
  OMX_REQUEST_STATE_MATCHED = (1<<1),
  OMX_REQUEST_STATE_QUEUED = (1<<2),
  OMX_REQUEST_STATE_IN_DRIVER = (1<<3),
  OMX_REQUEST_STATE_NEED_REPLY = (1<<4),
  OMX_REQUEST_STATE_RECV_UNEXPECTED = (1<<5),
  OMX_REQUEST_STATE_RECV_PARTIAL = (1<<6),
};

struct omx__generic_request {
  struct list_head queue_elt;
  struct list_head partner_elt;
  struct omx__partner * partner;
  enum omx__request_type type;
  uint32_t state;
  struct omx_status status;
};

union omx_request {
  struct omx__generic_request generic;

  struct {
    struct omx__generic_request generic;
    omx__seqnum_t seqnum;
    union {
      struct {
	void * buffer;
	uint32_t frags_pending_nr;
      } medium;
      struct {
	struct omx__large_region * region;
      } large;
    } specific;
  } send;

  struct {
    struct omx__generic_request generic;
    void * buffer;
    unsigned long length;
    uint64_t match_info;
    uint64_t match_mask;
    omx__seqnum_t seqnum;
    union {
      struct {
	uint32_t frags_received_mask;
	uint32_t accumulated_length;
      } medium;
      struct {
	struct omx__large_region * local_region;
	uint8_t target_rdma_id;
	uint8_t target_rdma_seqnum;
	uint16_t target_rdma_offset;
      } large;
    } specific;
  } recv;

  struct {
    struct omx__generic_request generic;
    uint32_t session_id;
    uint8_t connect_seqnum;
    int is_synchronous;
  } connect;
};

typedef void (*omx__process_recv_func_t) (struct omx_endpoint *ep,
					  struct omx__partner *partner,
					  union omx_request *req,
					  struct omx_evt_recv_msg *msg,
					  void *data, uint32_t msg_length);

struct omx__early_packet {
  struct list_head partner_elt;
  struct omx_evt_recv_msg msg;
  omx__process_recv_func_t recv_func;
  char * data;
  uint32_t msg_length;
};

struct omx__globals {
  int initialized;
  int control_fd;
  uint32_t board_max;
  uint32_t endpoint_max;
  uint32_t peer_max;
};

#endif /* __omx_types_h__ */
