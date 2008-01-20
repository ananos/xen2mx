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
      struct list_head reg_elt; /* linked into the endpoint reg_list or reg_vect_list */
      struct list_head reg_unused_elt; /* linked into the endpoint reg_unused_list if contigous, unused and cached */
      int use_count;
      uint8_t id;
      uint8_t seqnum;
      struct omx_cmd_region_segment *segs;
      uint16_t offset;
      void * reserver; /* single object that can be assigned (used for rndv/notify), while multiple pull may be pending */
    } region;
  } * array;
};

typedef uint16_t omx__seqnum_t; /* FIXME: assert same size on the wire */
/* 14 bits for sequence numbers */
#define OMX__SEQNUM_BITS 14
#define OMX__SEQNUM_MASK ((1UL<<OMX__SEQNUM_BITS)-1)
#define OMX__SEQNUM(x) ((x) & OMX__SEQNUM_MASK)
/* remaining bits for a partner session */
#define OMX__SESNUM_BITS (16-OMX__SEQNUM_BITS)
#define OMX__SESNUM_ONE (1UL<<OMX__SEQNUM_BITS)
#define OMX__SESNUM_MASK (((1UL<<OMX__SESNUM_BITS)-1)<<OMX__SEQNUM_BITS)
#define OMX__SESNUM(x) ((x) & OMX__SESNUM_MASK)

/* limit the seqnum offset of early packets,
 * and drop the other ones as obsolete from the previous wrap-around
 */
#define OMX__EARLY_PACKET_OFFSET_MAX 0xff

struct omx__partner {
  uint64_t board_addr;
  uint16_t peer_index;
  uint8_t endpoint_index;

  /* the main session id, obtained from the our actual connect */
  uint32_t true_session_id;
  /* another session id that we get from the connect request and use for
   * messages that can go back before we connect back (ack, pull and notify)
   */
  uint32_t back_session_id;

  /* seq num of the last connect request to this partner */
  uint8_t connect_seqnum;

  /* ack seqnums of last sent and recv explicit ack */
  uint32_t last_send_acknum;
  uint32_t last_recv_acknum;

  /* list of non-acked request (queued by their partner_elt) */
  struct list_head non_acked_req_q;
  /* pending connect requests (queued by their partner_elt) */
  struct list_head pending_connect_req_q;
  /* list of request matched but not entirely received (queued by their partner_elt) */
  struct list_head partial_recv_req_q;
  /* early packets (queued by their partner_elt) */
  struct list_head early_recv_q;

  /* seqnum of the next send */
  omx__seqnum_t next_send_seq;

  /* seqnum of the next send to be acked by the partner */
  omx__seqnum_t next_acked_send_seq;

  /* seqnum of the next new message to match
   * used to know to accumulate/match/defer a fragment
   */
  omx__seqnum_t next_match_recv_seq;

  /* seqnum of the next missing fragment to receive
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

  /* acks */
  uint64_t oldest_recv_time_not_acked;
  struct list_head endpoint_partners_to_ack_elt;

  /* user private data for get/set_endpoint_addr_context */
  void * user_context;
};

#define CTXID_FROM_MATCHING(ep, match) ((uint32_t)(((match) >> (ep)->ctxid_shift) & ((ep)->ctxid_max-1)))
#define CHECK_MATCHING_WITH_CTXID(ep, match) (((match) & (ep)->ctxid_mask) == (ep)->ctxid_mask)

struct omx_endpoint {
  int fd;
  int endpoint_index, board_index;
  char hostname[OMX_HOSTNAMELEN_MAX];
  char ifacename[OMX_IF_NAMESIZE];
  uint32_t app_key;
  omx_unexp_handler_t unexp_handler;
  void * unexp_handler_context;
  int in_handler;
  struct omx_endpoint_desc * desc;
  void * recvq, * sendq, * exp_eventq, * unexp_eventq;
  void * next_exp_event, * next_unexp_event;
  uint32_t avail_exp_events;
  uint32_t retransmit_delay_jiffies;
  uint32_t zombies, zombie_max;

  /* context ids */
  uint8_t ctxid_bits;
  uint32_t ctxid_max;
  uint8_t ctxid_shift;
  uint64_t ctxid_mask;

  /* context id array for multiplexed queues */
  struct {
    /* unexpected receive, may be partial (queued by their queue_elt) */
    struct list_head unexp_req_q;
    /* posted non-matched receive (queued by their queue_elt) */
    struct list_head recv_req_q;

    /* done requests (queued by their done_elt) */
    struct list_head done_req_q;
  } * ctxid;

  /* non multiplexed queues */
  /* SEND req with state = QUEUED (queued by their queue_elt) */
  struct list_head queued_send_req_q;
  /* SEND req with state = IN_DRIVER (queued by their queue_elt) */
  struct list_head driver_posted_req_q;
  /* RECV MEDIUM req with state = PARTIAL (queued by their queue_elt) */
  struct list_head multifrag_medium_recv_req_q;
  /* SEND LARGE req with state = NEED_REPLY and already acked (queued by their queue_elt) */
  struct list_head large_send_req_q;
  /* RECV_LARGE req with state = IN_DRIVER (queued by their queue_elt) */
  struct list_head pull_req_q;
  /* CONNECT req with state = NEED_REPLY (queued by their queue_elt) */
  struct list_head connect_req_q;
  /* any request that needs to be resent, thus NEED_ACK, and is not IN DRIVER (queued by their queue_elt) */
  struct list_head non_acked_req_q;
  /* any request that needs to be resent now, thus REQUEUED and NEED_ACK, not IN_DRIVER (queued by their queue_elt) */
  struct list_head requeued_send_req_q;
  /* send to self waiting for the matching */
  struct list_head send_self_unexp_req_q;

  struct omx__sendq_map sendq_map;
  struct omx__large_region_map large_region_map;
  struct omx__partner ** partners;
  struct omx__partner * myself;

  struct list_head partners_to_ack;

  struct list_head reg_list; /* registered single-segment windows */
  struct list_head reg_unused_list; /* unused registered single-segment windows, LRU in front */
  struct list_head reg_vect_list; /* registered vectorial windows (uncached) */
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
  OMX_REQUEST_TYPE_SEND_SELF,
  OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED,
};

/* Request states and queueing:
 * The request contains 3 queue elt:
 * + queue_elt: depends on the network state (need ack, need reply, posted to the driver, ...).
 *              used to queue the request on various endpoint queues (except the done_req_q)
 * + done_elt: used to queue the request on the endpoint done_req_q when the request is ready
 *             to be completed by by the application. It may happen before it is actually acked,
 *             and thus it is unrelated to where queue_elt is queued
 * + partner_elt: used to queue the request in the partner when it has not been acked yet
 *
 * The network state of the request determines where the queue_elt is queued:
 * SEND_TINY and SEND_SMALL:
 *   NEED_ACK: non_acked_req_q
 * SEND_MEDIUM:
 *   IN_DRIVER|NEED_ACK: driver_posted_req_q
 *   NEED_ACK: non_acked_req_q
 *   IN_DRIVER: driver_posted_req_q (unlikely)
 * SEND_LARGE:
 *   NEED_REPLY|NEED_ACK: non_acked_req_q
 *   NEED_REPLY: large_send_req_q
 *   NEED_ACK: non_acked_req_q
 * RECV_LARGE:
 *   IN_DRIVER: pull_req_q
 *   NEED_ACK: non_acked_req_q
 *   IN_DRIVER|NEED_ACK: impossible, we switch from one to the other in pull_done
 * CONNECT:
 *   NEED_REPLY: connect_req_q
 *
 * if REQUEUED added, resend_req_q instead of non_acked_req_q
 * if QUEUED, send_req_q
 *
 * The DONE qnd ZOMBIE states of the request determines whether the done_elt
 * is queued in the endpoint done_req_q:
 *   DONE: the done_elt is in the done_req_q and the request may complete it
 *   ZOMBIE: the done_elt has been removed from the done_req_q by the application completing
 *           the request earlier. the request is still waiting for some acks. it will not go back
 *           to the done_req_q when it arrives, it will just be freed.
 */

enum omx__request_state {
  /* placed on a queue for sending through the driver soon */
  OMX_REQUEST_STATE_QUEUED = (1<<0),
  /* posted to the driver, not done sending yet */
  OMX_REQUEST_STATE_IN_DRIVER = (1<<1),
  /* posted receive that didn't get match yet */
  OMX_REQUEST_STATE_RECV_NEED_MATCHING = (1<<2),
  /* partially received medium */
  OMX_REQUEST_STATE_RECV_PARTIAL = (1<<3),
  /* unexpected receive, needs to match a non-yet-posted receive */
  OMX_REQUEST_STATE_RECV_UNEXPECTED = (1<<4),
  /* needs an explicit reply from the peer */
  OMX_REQUEST_STATE_NEED_REPLY = (1<<5),
  /* needs a ack from the peer */
  OMX_REQUEST_STATE_NEED_ACK = (1<<6),
  /* placed on a queue for resending through the driver soon */
  OMX_REQUEST_STATE_REQUEUED = (1<<7),
  /* request can already be completed by the application, even if not acked yet */
  OMX_REQUEST_STATE_DONE = (1<<8),
  /* request has been completed by the application and should not be notified when done for real (including acked) */
  OMX_REQUEST_STATE_ZOMBIE = (1<<9),
  /* request is internal, should not be queued in the doneq for peek/test_any */
  OMX_REQUEST_STATE_INTERNAL = (1<<10),
  /* request is a send to myself, needs to wait for the recv to match */
  OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED = (1<<11),
};

struct omx__generic_request {
  /* main queue elt, linked to one of the endpoint queues */
  struct list_head queue_elt;
  /* done queue elt, queued to the endpoint doneq when ready to be completed */
  struct list_head done_elt;
  /* partner specific queue elt, either for partial receive, or for non-acked request (cannot be both) */
  struct list_head partner_elt;

  struct omx__partner * partner;
  enum omx__request_type type;
  omx__seqnum_t send_seqnum; /* seqnum of the sent message associated with the request, either for a usual send request, or the notify message for recv large */
  uint64_t submit_jiffies;
  uint64_t last_send_jiffies;
  uint32_t retransmit_delay_jiffies;
  uint32_t state;
  struct omx_status status;
};

struct omx__req_seg {
    omx_seg_t single; /* optimization to store the single segment */
    uint32_t nseg;
    omx_seg_t * segs;
    uint32_t total_length;
};

/* current segment and offset within an array of segments */
struct omx_segscan_state {
  omx_seg_t *seg;
  uint32_t offset;
};

union omx_request {
  struct omx__generic_request generic;

  struct {
    struct omx__generic_request generic;
    struct omx__req_seg segs;
    union {
      struct {
	struct omx_cmd_send_tiny send_tiny_ioctl_param;
      } tiny;
      struct {
	struct omx_cmd_send_small send_small_ioctl_param;
	void *copy; /* buffered data attached the request */
      } small;
      struct {
	struct omx_cmd_send_medium send_medium_ioctl_param;
	uint32_t frags_nr;
	uint32_t frags_pending_nr;
	int sendq_map_index[8]; /* FIXME #define NR_MEDIUM_FRAGS */
      } medium;
      struct {
	struct omx_cmd_send_rndv send_rndv_ioctl_param;
	struct omx__large_region * region;
      } large;
    } specific;
  } send;

  struct {
    struct omx__generic_request generic;
    struct omx__req_seg segs;
    uint64_t match_info;
    uint64_t match_mask;
    omx__seqnum_t seqnum; /* seqnum of the incoming matched send */
    union {
      struct {
	uint32_t frags_received_mask;
	uint32_t accumulated_length;
	uint32_t scan_offset;
	struct omx_segscan_state scan_state;
      } medium;
      struct {
	struct omx_cmd_send_notify send_notify_ioctl_param;
	struct omx__large_region * local_region;
	uint8_t target_rdma_id;
	uint8_t target_rdma_seqnum;
	uint16_t target_rdma_offset;
      } large;
      struct {
	union omx_request *sreq;
      } self_unexp;
    } specific;
  } recv;

  struct {
    struct omx__generic_request generic;
    struct omx_cmd_send_connect send_connect_ioctl_param;
    uint32_t session_id;
    uint8_t connect_seqnum;
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
  int verbose;
  int regcache;
  int waitspin;
  int zombie_max;
  int selfcomms;
  unsigned ack_delay;
  unsigned resend_delay;
  unsigned retransmits_max;
};

#endif /* __omx_types_h__ */
