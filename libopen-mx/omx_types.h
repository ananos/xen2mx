/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2010 (see AUTHORS file)
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
#include "omx_threads.h"

/*****************
 * Internal types
 */

struct omx__req_segs {
  struct omx_cmd_user_segment single; /* optimization to store the single segment */
  uint32_t nseg;
  struct omx_cmd_user_segment *segs;
  uint32_t total_length;
};

/* current segment and offset within an array of segments */
struct omx_segscan_state {
  struct omx_cmd_user_segment *seg;
  uint32_t offset;
};

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
      uint8_t last_seqnum;
      struct omx__req_segs segs;
      void * reserver; /* single object that can be assigned (used for rndv/notify), while multiple pull may be pending */
    } region;
  } * array;
};

typedef uint16_t omx__seqnum_t;
/* 14 bits for sequence numbers */
#define OMX__SEQNUM_BITS 14
#define OMX__SEQNUM_MASK ((1UL<<OMX__SEQNUM_BITS)-1)
#define OMX__SEQNUM(x) ((x) & OMX__SEQNUM_MASK)
/* remaining bits for a partner session */
#define OMX__SESNUM_BITS (16-OMX__SEQNUM_BITS)
#define OMX__SESNUM_ONE (1UL<<OMX__SEQNUM_BITS)
#define OMX__SESNUM_MASK (((1UL<<OMX__SESNUM_BITS)-1)<<OMX__SEQNUM_BITS)
#define OMX__SESNUM(x) ((x) & OMX__SESNUM_MASK)
#define OMX__SESNUM_SHIFTED(x) (OMX__SESNUM(x) >> OMX__SEQNUM_BITS)

#define OMX__SEQNUM_INCREASE_BY(x,n) do {      \
  omx__seqnum_t old = x;                       \
  x = OMX__SESNUM(old) | OMX__SEQNUM(old + n); \
} while (0)

#define OMX__SEQNUM_INCREASE(x) OMX__SEQNUM_INCREASE_BY(x,1)

#define OMX__SEQNUM_RESET(x) do {        \
  omx__seqnum_t old = x;                 \
  x = OMX__SESNUM(old) | OMX__SEQNUM(1); \
} while (0)

/* limit the seqnum offset of early packets,
 * and drop the other ones as obsolete from the previous wrap-around
 */
#define OMX__EARLY_PACKET_OFFSET_MAX 0xff

/* limit the seqnum of non-acked send, throttle other sends.
 * it also limits the number of possible partial recv in the remote side,
 * which means we don't have to check/throttle there
 */
#define OMX__THROTTLING_OFFSET_MAX (OMX__SEQNUM_MASK/2)

enum omx__partner_localization {
  OMX__PARTNER_LOCALIZATION_LOCAL,
  OMX__PARTNER_LOCALIZATION_REMOTE,
  OMX__PARTNER_LOCALIZATION_UNKNOWN
};

enum omx__partner_need_ack {
  OMX__PARTNER_NEED_NO_ACK,
  OMX__PARTNER_NEED_ACK_DELAYED,
  OMX__PARTNER_NEED_ACK_IMMEDIATE
};

struct omx__partner {
  uint64_t board_addr;
  uint16_t peer_index;
  uint8_t endpoint_index;
  uint8_t localization;
  uint16_t rndv_threshold;

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
  struct list_head connect_req_q;
  /* list of request matched but not entirely received (queued by their partner_elt) */
  struct list_head partial_medium_recv_req_q;
  /* delayed send because of throttling (too many acks missing) (queued by their partner_elt) */
  struct list_head need_seqnum_send_req_q;

  /* early packets (queued by their partner_elt) */
  struct list_head early_recv_q;

  /* throttling state */
  uint32_t throttling_sends_nr;
  struct list_head endpoint_throttling_partners_elt;

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

  /* seqnum of the next seqnum used in the last ack/piggyack that we sent */
  omx__seqnum_t last_acked_recv_seq;

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
  struct list_head endpoint_partners_to_ack_elt;
  enum omx__partner_need_ack need_ack;
  /* when a ack is need but not immediately (need_ack == ACK_DELAYED) */
  uint64_t oldest_recv_time_not_acked;

  /* user private data for get/set_endpoint_addr_context */
  void * user_context;
};

/* the internal structure hidden behind an API omx_endpoint_addr */
struct omx__endpoint_addr {
  struct omx__partner * partner;
  char pad[sizeof(struct omx_endpoint_addr) - sizeof(struct omx__partner *) - sizeof(uint32_t)];
  uint32_t session_id;
  /* pad to the exact API size to avoid aliasing problems */
} __may_alias;

#define HAS_CTXIDS(ep) ((ep)->ctxid_bits > 0)
#define MATCHING_CROSS_CTXIDS(ep, match) (((match) & (ep)->ctxid_mask) != 0)
#define CTXID_FROM_MATCHING(ep, match) ((uint32_t)(((match) >> (ep)->ctxid_shift) & ((ep)->ctxid_max-1)))

#define OMX_PROGRESSION_DISABLED_IN_HANDLER (1<<0)
#define OMX_PROGRESSION_DISABLED_BY_API (1<<1)

/* the order must follow allocation order in submit/post routines */
enum omx__request_resource {
  /* medium send and pull requests need expected event slots */
  OMX_REQUEST_RESOURCE_EXP_EVENT = (1<<0),
  /* large send requests need a send-specific region slot */
  OMX_REQUEST_RESOURCE_SEND_LARGE_REGION = (1<<1),
  /* large requests need a large region */
  OMX_REQUEST_RESOURCE_LARGE_REGION = (1<<2),
  /* pull requests need kernel handle */
  OMX_REQUEST_RESOURCE_PULL_HANDLE = (1<<3),
  /* mediumsq send requests need sendq slots */
  OMX_REQUEST_RESOURCE_SENDQ_SLOT = (1<<4)
};

#define OMX_REQUEST_SEND_MEDIUMSQ_RESOURCES (OMX_REQUEST_RESOURCE_EXP_EVENT | OMX_REQUEST_RESOURCE_SENDQ_SLOT)
#define OMX_REQUEST_SEND_LARGE_RESOURCES (OMX_REQUEST_RESOURCE_SEND_LARGE_REGION | OMX_REQUEST_RESOURCE_LARGE_REGION)
#define OMX_REQUEST_PULL_RESOURCES (OMX_REQUEST_RESOURCE_EXP_EVENT | OMX_REQUEST_RESOURCE_LARGE_REGION | OMX_REQUEST_RESOURCE_PULL_HANDLE)

struct omx_endpoint {
  int fd;
  unsigned endpoint_index, board_index;
  struct omx_board_info board_info;
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  uint32_t app_key;
  struct omx__lock lock;
  int progression_disabled;
  struct omx__cond in_handler_cond;
  omx_unexp_handler_t unexp_handler;
  void * unexp_handler_context;
  struct omx_endpoint_desc * desc;
  uint32_t check_status_delay_jiffies;
  uint64_t last_check_jiffies;
#ifdef OMX_LIB_DEBUG
  uint64_t last_progress_jiffies;
#endif
  void * recvq, * sendq, * exp_eventq, * unexp_eventq;
  void * next_exp_event, * next_unexp_event;
  uint8_t  next_exp_event_id, next_unexp_event_id;
  uint32_t avail_exp_events;
  uint32_t req_resends_max;
  uint32_t pull_resend_timeout_jiffies;
  uint32_t zombies, zombie_max;

  /* context ids */
  uint8_t ctxid_bits;
  uint32_t ctxid_max;
  uint8_t ctxid_shift;
  uint64_t ctxid_mask;

  /* global queues that contain all ctxid queues */
  struct {
    /* done requests (queued by their done_elt) */
    struct list_head done_req_q;
    /* unexpected receive, may be partial (queued by their queue_elt) */
    struct list_head unexp_req_q;
  } anyctxid;

  /* context id array for multiplexed queues */
  struct {
    /* unexpected receive, may be partial (queued by their ctxid_elt, only if there are multiple ctxids) */
    struct list_head unexp_req_q;
    /* posted non-matched receive (queued by their queue_elt) */
    struct list_head recv_req_q;

    /* done requests (queued by their ctxid_elt, only if there are multiple ctxids) */
    struct list_head done_req_q;
  } * ctxid;

  /* non multiplexed queues */
  /* SEND req with state = NEED_RESOURCES (queued by their queue_elt) */
  struct list_head need_resources_send_req_q;
  /* SEND MEDIUMSQ req with state = DRIVER_MEDIUMSQ_SENDING (queued by their queue_elt) */
  struct list_head driver_mediumsq_sending_req_q;
  /* SEND LARGE req with state = NEED_REPLY and already acked (queued by their queue_elt) */
  struct list_head large_send_need_reply_req_q;
  /* RECV_LARGE req with state = DRIVER_PULLING (queued by their queue_elt) */
  struct list_head driver_pulling_req_q;
  /* any connect request that needs to be resent, thus NEED_REPLY (queued by their queue_elt) */
  struct list_head connect_req_q;
  /* any send request that needs to be resent, thus NEED_ACK, and is not DRIVER_MEDIUMSQ_SENDING (queued by their queue_elt) */
  struct list_head non_acked_req_q;
  /* send to self waiting for the matching (queued by their queue_elt) */
  struct list_head unexp_self_send_req_q;

#ifdef OMX_LIB_DEBUG
  /* two debug queues so that a request queue_elt is always queued somewhere */
  /* RECV MEDIUM req with state = PARTIAL (queued by their queue_elt) */
  struct list_head partial_medium_recv_req_q;
  /* SEND req with state = NEED_SEQNUM (queued by their queue_elt) */
  struct list_head need_seqnum_send_req_q;
  /* any request with state == DONE (done for real, not early, not zombie) (queued by their queue_elt) */
  struct list_head really_done_req_q;
  /* internal DONE requests (synchronous connect) */
  struct list_head internal_done_req_q;
#endif

  struct omx__sendq_map sendq_map;
  struct omx__large_region_map large_region_map;
  struct omx__partner ** partners;
  struct omx__partner * myself;

  uint64_t last_partners_acking_jiffies;
  struct list_head partners_to_ack_immediate_list;
  struct list_head partners_to_ack_delayed_list;
  struct list_head throttling_partners_list;

  struct list_head sleepers;

  struct list_head reg_list; /* registered single-segment windows */
  struct list_head reg_unused_list; /* unused registered single-segment windows, LRU in front */
  struct list_head reg_vect_list; /* registered vectorial windows (uncached) */
  int large_sends_avail_nr; /* number of simultaneous large send that may be posted,
			     * limited to prevent deadlocks */

  omx_error_handler_t error_handler;

  struct list_head omx_endpoints_list_elt;

#ifdef OMX_LIB_DEBUG
  unsigned int req_alloc_nr;
#endif
  char *message_prefix;
};

#define OMX__ENDPOINT_LOCK(ep) omx__lock(&(ep)->lock)
#define OMX__ENDPOINT_UNLOCK(ep) omx__unlock(&(ep)->lock)
#define OMX__ENDPOINT_HANDLER_DONE_WAIT(ep) omx__cond_wait(&(ep)->in_handler_cond, &(ep)->lock)
#define OMX__ENDPOINT_HANDLER_DONE_SIGNAL(ep) omx__cond_signal(&(ep)->in_handler_cond)

enum omx__request_type {
  OMX_REQUEST_TYPE_NONE=0,
  OMX_REQUEST_TYPE_CONNECT,
  OMX_REQUEST_TYPE_SEND_TINY,
  OMX_REQUEST_TYPE_SEND_SMALL,
  OMX_REQUEST_TYPE_SEND_MEDIUMSQ,
  OMX_REQUEST_TYPE_SEND_MEDIUMVA,
  OMX_REQUEST_TYPE_SEND_LARGE,
  OMX_REQUEST_TYPE_RECV,
  OMX_REQUEST_TYPE_RECV_LARGE,
  OMX_REQUEST_TYPE_SEND_SELF,
  OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED
};

/* Request states and queueing:
 * The request contains 3 queue elt:
 * + queue_elt: depends on the network state (need ack, need reply, posted to the driver, ...).
 *              used to queue the request on various endpoint queues (except the really_done_req_q)
 * + done_elt: used to queue the request on the endpoint done_req_q when the request is ready
 *             to be completed by by the application. It may happen before it is actually acked,
 *             and thus it is unrelated to where queue_elt is queued
 * + partner_elt: used to queue the request in the partner when it has not been acked yet
 *
 * The network state of the request determines where the queue_elt is queued:
 * SEND_TINY and SEND_SMALL:
 *   NEED_ACK: ep->non_acked_req_q + partner->non_acked_req_q
 * SEND_MEDIUMSQ:
 *   DRIVER_MEDIUMSQ_SENDING | NEED_ACK: ep->driver_medium_sending_req_q + partner->non_acked_req_q
 *               (not on ep->non_acked_req_q since should not be resend before being done sending)
 *   NEED_ACK: ep->non_acked_req_q + partner->non_acked_req_q
 *   DRIVER_MEDIUMSQ_SENDING (unlikely): ep->driver_mediumsq_sending_req_q
 * SEND_LARGE:
 *   NEED_REPLY | NEED_ACK: ep->non_acked_req_q + partner->non_acked_req_q
 *   NEED_REPLY: ep->large_send_req_q
 *   NEED_ACK (unlikely): ep->non_acked_req_q + partner->non_acked_req_q
 * RECV (not RECV_LARGE):
 *   UNEXPECTED_RECV: ep->unexp_req_q
 *   UNEXPECTED_RECV | RECV_PARTIAL: ep->unexp_req_q + partner->partial_medium_recv_req_q
 *   RECV_PARTIAL: ep->partial_medium_recv_req_q(DBG) + partner->partial_medium_recv_req_q
 * RECV_LARGE:
 *   DRIVER_PULLING: ep->driver_pulling_req_q
 *   NEED_ACK: ep->non_acked_req_q + partner->non_acked_req_q
 *   DRIVER_PULLING | NEED_ACK: impossible, we switch from one to the other in pull_done
 *   RECV_PARTIAL added if not pulling yet
 * CONNECT:
 *   NEED_REPLY: ep->connect_req_q + partner->connect_req_q
 *
 * Before being posted for real, all send requests (and recv large notifying) may be:
 * NEED_RESOURCES: ep->need_resources_send_req_q
 * NEED_SEQNUM: ep->need_seqnum_send_req_q(DBG) + partner->need_seqnum_send_req_q
 *
 * The DONE and ZOMBIE states of the request determines whether the done_elt
 * is queued in the endpoint done_req_q:
 *   DONE: the done_elt is in the done_req_q and the request may complete it
 *   ZOMBIE: the done_elt has been removed from the done_req_q by the application completing
 *           the request earlier. the request is still waiting for some acks. it will not go back
 *           to the done_req_q when it arrives, it will just be freed.
 */

enum omx__request_state {
  /* placed on a queue for delayed posting due to resource shortage */
  OMX_REQUEST_STATE_NEED_RESOURCES = (1<<0),
  /* request is a send to a partner which didn't ack enough yet */
  OMX_REQUEST_STATE_NEED_SEQNUM = (1<<1),
  /* posted mediumsq frag to the driver, not done sending yet */
  OMX_REQUEST_STATE_DRIVER_MEDIUMSQ_SENDING = (1<<2),
  /* needs a ack from the peer */
  OMX_REQUEST_STATE_NEED_ACK = (1<<3),
  /* needs an explicit reply from the peer, either send large or connect */
  OMX_REQUEST_STATE_NEED_REPLY = (1<<4),
  /* posted receive that didn't get match yet */
  OMX_REQUEST_STATE_RECV_NEED_MATCHING = (1<<5),
  /* partially received medium */
  OMX_REQUEST_STATE_RECV_PARTIAL = (1<<6),
  /* posted pull to the driver, not done pulling yet */
  OMX_REQUEST_STATE_DRIVER_PULLING = (1<<7),
  /* request is a send to myself, needs to wait for the recv to match */
  OMX_REQUEST_STATE_UNEXPECTED_RECV = (1<<8),
  /* request can already be completed by the application, even if not acked yet */
  OMX_REQUEST_STATE_UNEXPECTED_SELF_SEND = (1<<9),
  /* unexpected receive, needs to match a non-yet-posted receive */
  OMX_REQUEST_STATE_DONE = (1<<10),
  /* request has been completed by the application and should not be notified when done for real (including acked) */
  OMX_REQUEST_STATE_ZOMBIE = (1<<11),
  /* request is internal, should not be queued in the doneq for peek/test_any */
  OMX_REQUEST_STATE_INTERNAL = (1<<12)
};

struct omx__generic_request {
  /* main queue elt, linked to one of the endpoint queues */
  struct list_head queue_elt;
  /* done queue elt, queued to the endpoint main doneq when ready to be completed */
  struct list_head done_elt;
  /* queue for specific ctxid elt, queued to an endpoint ctxid doneq when ready to be completed */
  struct list_head ctxid_elt;
  /* partner specific queue elt, either for partial receive, or for non-acked request (cannot be both) */
  struct list_head partner_elt;

  struct omx__partner * partner;
  enum omx__request_type type;
  uint16_t state;
  uint16_t missing_resources;

  omx__seqnum_t send_seqnum; /* seqnum of the sent message associated with the request, either for a usual send request, or the notify message for recv large */
  uint64_t last_send_jiffies;
  uint32_t resends_max;
  uint32_t resends;

  struct omx_status status;
};

#define OMX_MEDIUM_FRAGS_MAX 32 /* 32 are needed if MTU=1500, only 8 is the regular case */

union omx_request {
  struct omx__generic_request generic;

  struct omx__send_request {
    struct omx__generic_request generic;
    struct omx__req_segs segs;
    union {
      struct {
	struct omx_cmd_send_tiny send_tiny_ioctl_param;
      } tiny;
      struct {
	struct omx_cmd_send_small send_small_ioctl_param;
	void *copy; /* buffered data attached the request */
      } small;
      struct {
	struct omx_cmd_send_mediumsq_frag send_mediumsq_frag_ioctl_param;
	uint32_t frags_nr;
	uint32_t frags_pending_nr;
#ifdef OMX_MX_WIRE_COMPAT
	unsigned frag_pipeline;
#endif
	int sendq_map_index[OMX_MEDIUM_FRAGS_MAX];
      } mediumsq;
      struct {
	struct omx_cmd_send_mediumva send_mediumva_ioctl_param;
      } mediumva;
      struct {
	struct omx_cmd_send_rndv send_rndv_ioctl_param;
	struct omx__large_region * region;
	uint8_t region_seqnum;
      } large;
    } specific;
  } send;

  struct omx__recv_request {
    struct omx__generic_request generic;
    struct omx__req_segs segs;
    uint64_t match_info;
    uint64_t match_mask;
    uint16_t checksum; /* checksum given by sender in incoming send */
    omx__seqnum_t seqnum; /* seqnum of the incoming matched send */
    union {
      struct {
	uint32_t frags_received_mask;
	uint32_t accumulated_length; /* the actual received length, not the transfered one */
	uint32_t scan_offset;
	struct omx_segscan_state scan_state;
      } medium;
      struct {
	struct omx_cmd_send_notify send_notify_ioctl_param;
	struct omx__large_region * local_region;
	uint8_t pulled_rdma_id;
	uint8_t pulled_rdma_seqnum;
	uint16_t pulled_rdma_offset;
      } large;
      struct {
	union omx_request *sreq;
      } self_unexp;
    } specific;
  } recv;

  struct omx__connect_request {
    struct omx__generic_request generic;
    struct omx_cmd_send_connect_request send_connect_request_ioctl_param;
    uint32_t session_id;
    uint8_t connect_seqnum;
  } connect;
};

typedef void (*omx__process_recv_func_t) (struct omx_endpoint *ep,
					  struct omx__partner *partner,
					  union omx_request *req,
					  const struct omx_evt_recv_msg *msg,
					  const void *data, uint32_t xfer_length);

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
  int ignore_mx_env;
  int verbose;
  int verbdebug;
  int regcache;
  int parallel_regcache;
  int waitspin;
  int connect_pollall;
  int zombie_max;
  int waitintr;
  int fatal_errors;
  int debug_signal_level;
  int debug_checksum;
  int check_request_alloc;
  int medium_sendq;
  uint32_t any_endpoint_id;
  int selfcomms;
  int sharedcomms;
  unsigned rndv_threshold;
  unsigned shared_rndv_threshold;
  unsigned ack_delay_jiffies;
  unsigned resend_delay_jiffies;
  unsigned req_resends_max;
  unsigned not_acked_max;
  unsigned ctxid_bits;
  unsigned ctxid_shift;
  char *process_binding;
  char *message_prefix;
  char *message_prefix_format;
  unsigned abort_sleeps;
};

#define OMX_INTERNAL_RETURN_CODE_MIN ((omx_return_t) 101)
#define OMX_INTERNAL_MISSING_RESOURCES ((omx_return_t) 102)
#define OMX_INTERNAL_UNEXPECTED_ERRNO ((omx_return_t) 103)
#define OMX_INTERNAL_MISC_ENODEV ((omx_return_t) 104)
#define OMX_INTERNAL_MISC_EINVAL ((omx_return_t) 105)
#define OMX_INTERNAL_MISC_EFAULT ((omx_return_t) 106)

#endif /* __omx_types_h__ */
