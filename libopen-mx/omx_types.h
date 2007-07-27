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

typedef uint16_t omx__seqnum_t; /* FIXME: assert same size on the wire */

struct omx__partner {
  uint64_t board_addr;
  uint16_t peer_index;
  uint8_t endpoint_index;

  /* list of request matched but not entirely received */
  struct list_head partialq;

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

};

struct omx_endpoint {
  int fd;
  int endpoint_index, board_index;
  char board_name[OMX_HOSTNAMELEN_MAX];
  void * recvq, * sendq, * eventq;
  void * next_event;
  struct list_head sent_req_q;
  struct list_head unexp_req_q;
  struct list_head recv_req_q;
  struct list_head multifraq_medium_recv_req_q;
  struct list_head connect_req_q;
  struct list_head done_req_q;
  struct omx__sendq_map sendq_map;
  struct omx__partner ** partners;
  struct omx__partner * myself;
};

enum omx__request_type {
  OMX_REQUEST_TYPE_NONE=0,
  OMX_REQUEST_TYPE_CONNECT,
  OMX_REQUEST_TYPE_SEND_TINY,
  OMX_REQUEST_TYPE_SEND_SMALL,
  OMX_REQUEST_TYPE_SEND_MEDIUM,
  OMX_REQUEST_TYPE_RECV,
};

enum omx__request_state {
  OMX_REQUEST_STATE_PENDING=0,
  OMX_REQUEST_STATE_DONE,
};

struct omx__generic_request {
  struct list_head queue_elt;
  enum omx__request_type type;
  enum omx__request_state state;
  struct omx_status status;
};

union omx_request {
  struct omx__generic_request generic;

  struct {
    struct omx__generic_request generic;
    struct omx__partner * partner;
    omx__seqnum_t seqnum;
    union {
      struct {
	uint32_t frags_pending_nr;
      } medium;
    } type;
  } send;

  struct {
    struct omx__generic_request generic;
    void * buffer;
    unsigned long length;
    union {
      struct {
	uint32_t frags_received_mask;
	uint32_t accumulated_length;
      } medium;
    } type;
  } recv;

  struct {
    struct omx__generic_request generic;
    struct omx__partner * partner;
  } connect;
};

struct omx__globals {
  int initialized;
  int control_fd;
  uint32_t board_max;
  uint32_t endpoint_max;
  uint32_t peer_max;
};

#endif /* __omx_types_h__ */
