/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

#include "omx_lib.h"
#include "omx_segments.h"
#include "omx_request.h"
#include "omx_wire_access.h"
#include "omx_lib_wire.h"

/*********************
 * Receive completion
 */

void
omx__recv_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_return_t status)
{
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);

  if (likely(req->generic.status.code == OMX_SUCCESS)) {
    /* only set the status if it is not already set to an error */
    if (likely(status == OMX_SUCCESS)) {
      if (unlikely(req->generic.status.xfer_length < req->generic.status.msg_length))
	req->generic.status.code = omx__error_with_req(ep, req, OMX_MESSAGE_TRUNCATED,
						       "Completing receive request, truncated from %ld to %ld bytes",
						       req->generic.status.msg_length, req->generic.status.xfer_length);
    } else {
      req->generic.status.code = omx__error_with_req(ep, req, status, "Completing receive request");
    }
  }

  /* the request is done, we can free the segments */
  omx_free_segments(&req->send.segs);

  omx__notify_request_done(ep, ctxid, req);
}

/****************
 * Early packets
 */

/* find which early which need to queue the new one after,
 * or drop if duplicate
 */
static INLINE struct list_head *
omx__find_previous_early_packet(struct omx__partner * partner,
				struct omx_evt_recv_msg *msg)
{
  omx__seqnum_t seqnum = msg->seqnum;
  omx__seqnum_t next_match_recv_seq = partner->next_match_recv_seq;
  struct omx__early_packet * current;
  omx__seqnum_t new_index;
  omx__seqnum_t current_index;

  /* trivial case, early queue is empty */
  if (omx__empty_partner_early_packet_queue(partner)) {
    omx__debug_printf(EARLY, "insert early in empty queue\n");
    return &partner->early_recv_q;
  }

  new_index = OMX__SEQNUM(seqnum - next_match_recv_seq);

  /* a little bit less trivial case, append at the end */
  current = omx__last_partner_early_packet(partner);
  current_index = OMX__SEQNUM(current->msg.seqnum - next_match_recv_seq);
  if (new_index > current_index) {
    omx__debug_printf(EARLY, "inserting early at the end of queue\n");
    return partner->early_recv_q.prev;
  }

  /* a little bit less trivial case, append at the beginning */
  current = omx__first_partner_early_packet(partner);
  current_index = OMX__SEQNUM(current->msg.seqnum - next_match_recv_seq);
  if (new_index < current_index) {
    omx__debug_printf(EARLY, "inserting early at the beginning of queue\n");
    return &partner->early_recv_q;
  }

  /* general case, add at the right position, and drop if duplicate */
  omx__foreach_partner_early_packet_reverse(partner, current) {
    current_index = OMX__SEQNUM(current->msg.seqnum - next_match_recv_seq);

    if (new_index > current_index) {
      /* found an earlier one, insert after it */
      omx__debug_printf(EARLY, "inserting early after another one\n");
      return &current->partner_elt;
    }

    if (new_index < current_index) {
      /* later one, look further */
      omx__debug_printf(EARLY, "not inserting early after this one\n");
      continue;
    }

    if (msg->type == OMX_EVT_RECV_MEDIUM) {
      /* medium early, check the frag num */
      unsigned long current_frag_seqnum = current->msg.specific.medium.frag_seqnum;
      unsigned long new_frag_seqnum = msg->specific.medium.frag_seqnum;

      if (new_frag_seqnum > current_frag_seqnum) {
	/* found an earlier one, insert after it */
	omx__debug_printf(EARLY, "inserting early after this medium\n");
	return &current->partner_elt;
      }

      if (new_frag_seqnum < current_frag_seqnum) {
	/* later one, look further */
	omx__debug_printf(EARLY, "not inserting early after this medium\n");
	continue;
      }

      /* that's a duplicate medium frag, drop it */
      omx__debug_printf(EARLY, "dropping duplicate early medium\n");
      return NULL;
    }

    /* that's a duplicate, drop it */
    omx__debug_printf(EARLY, "dropping duplicate early\n");
    return NULL;
  }

  omx__abort("Found no previous early");
}

static INLINE void
omx__postpone_early_packet(struct omx__partner * partner,
			   struct omx_evt_recv_msg *msg, void *data,
			   omx__process_recv_func_t recv_func)
{
  struct omx__early_packet * early;
  struct list_head * prev;

  prev = omx__find_previous_early_packet(partner, msg);
  if (!prev)
    /* obsolete early ? ignore */
    return;

  early = malloc(sizeof(*early));
  if (unlikely(!early))
    /* cannot store early? just drop, it will be resent */
    return;

  /* copy the whole event, the callback, and the data */
  memcpy(&early->msg, msg, sizeof(*msg));
  early->recv_func = recv_func;

  /* no data allocated by default */
  early->data = NULL;

  switch (msg->type) {
  case OMX_EVT_RECV_TINY:
    /* no need to set early->data, omx__process_recv_tiny
     * always takes the data from inside the event
     */
    early->msg_length = msg->specific.tiny.length;
    break;

  case OMX_EVT_RECV_SMALL: {
    uint16_t length = msg->specific.small.length;
    char * early_data = malloc(length);
    if (!early_data) {
      free(early);
      /* cannot store early? just drop, it will be resent */
      return;
    }
    memcpy(early_data, data, length);
    early->data = early_data;
    early->msg_length = length;
    break;
  }

  case OMX_EVT_RECV_MEDIUM: {
    uint16_t frag_length = msg->specific.medium.frag_length;
    char * early_data = malloc(frag_length);
    if (unlikely(!early_data)) {
      free(early);
      /* cannot store early? just drop, it will be resent */
      return;
    }
    memcpy(early_data, data, frag_length);
    early->data = early_data;
    early->msg_length = msg->specific.medium.msg_length;
    break;
  }

  case OMX_EVT_RECV_RNDV: {
    struct omx__rndv_data * data_n = (void *) msg->specific.rndv.data;
    uint32_t msg_length = OMX_FROM_PKT_FIELD(data_n->msg_length);
    early->msg_length = msg_length;
    break;
  }

  case OMX_EVT_RECV_NOTIFY: {
    /* cannot be unexpected but can still be early if the previous messages got lost */
    break;
  }

  default:
    omx__abort("Failed to handle early packet with type %d\n",
	       msg->type);
  }

  omx__debug_printf(EARLY, "postponing early packet with seqnum %d (#%d)\n",
		    (unsigned) OMX__SEQNUM(msg->seqnum),
		    (unsigned) OMX__SESNUM_SHIFTED(msg->seqnum));

  list_add(&early->partner_elt, prev);
}

/*****************************************
 * Packet-type-specific receive callbacks
 */

void
omx__process_recv_tiny(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       struct omx_evt_recv_msg *msg,
		       void *data /* unused */, uint32_t msg_length)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, msg->match_info);

  omx_copy_to_segments(&req->recv.segs, msg->specific.tiny.data, msg_length);

  if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED))
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
  else
    omx__recv_complete(ep, req, OMX_SUCCESS);
}

void
omx__process_recv_small(struct omx_endpoint *ep, struct omx__partner *partner,
			union omx_request *req,
			struct omx_evt_recv_msg *msg,
			void *data, uint32_t msg_length)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, msg->match_info);

  omx_copy_to_segments(&req->recv.segs, data, msg_length);

  if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED))
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
  else
    omx__recv_complete(ep, req, OMX_SUCCESS);
}

static INLINE void
omx__init_process_recv_medium(union omx_request *req)
{
  req->recv.specific.medium.frags_received_mask = 0;
  req->recv.specific.medium.accumulated_length = 0;
  /* initialize the state to the beginning */
  req->recv.specific.medium.scan_offset = 0;
  req->recv.specific.medium.scan_state.seg = &req->recv.segs.segs[0];
  req->recv.specific.medium.scan_state.offset = 0;
}

void
omx__process_recv_medium_frag(struct omx_endpoint *ep, struct omx__partner *partner,
			      union omx_request *req,
			      struct omx_evt_recv_msg *msg,
			      void *data, uint32_t msg_length)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, msg->match_info);
  unsigned long chunk = msg->specific.medium.frag_length;
  unsigned long frag_seqnum = msg->specific.medium.frag_seqnum;
  unsigned long frag_pipeline = msg->specific.medium.frag_pipeline;
  unsigned long offset = frag_seqnum << frag_pipeline;
  int new = (req->recv.specific.medium.frags_received_mask == 0);

  omx__debug_printf(MEDIUM, "got a medium frag seqnum %d pipeline %d length %d offset %d of total %d\n",
		    (unsigned) frag_seqnum, (unsigned) frag_pipeline, (unsigned) chunk,
		    (unsigned) offset, (unsigned) msg_length);

  if (unlikely(req->recv.specific.medium.frags_received_mask & (1 << frag_seqnum))) {
    /* already received this frag, requeue back */
    omx__debug_printf(MEDIUM, "got a duplicate frag seqnum %d for medium seqnum %d (#%d)\n",
		      (unsigned) frag_seqnum,
		      (unsigned) OMX__SEQNUM(req->recv.seqnum),
		      (unsigned) OMX__SESNUM_SHIFTED(req->recv.seqnum));
    omx__enqueue_request(unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED)
			 ? &ep->ctxid[ctxid].unexp_req_q : &ep->multifrag_medium_recv_req_q,
			 req);
    return;
  }

  /* take care of the data chunk */
  if (unlikely(offset + chunk > msg_length))
    chunk = msg_length - offset;

  if (likely(req->recv.segs.nseg == 1))
    memcpy(req->recv.segs.single.ptr + offset, data, chunk);
  else
    omx_partial_copy_to_segments(&req->recv.segs, data, chunk,
				 offset, &req->recv.specific.medium.scan_state,
				 &req->recv.specific.medium.scan_offset);
  req->recv.specific.medium.frags_received_mask |= 1 << frag_seqnum;
  req->recv.specific.medium.accumulated_length += chunk;

  if (likely(req->recv.specific.medium.accumulated_length == msg_length)) {
    /* was the last frag */
    omx__debug_printf(MEDIUM, "got last frag of seqnum %d (#%d)\n",
		      (unsigned) OMX__SEQNUM(req->recv.seqnum),
		      (unsigned) OMX__SESNUM_SHIFTED(req->recv.seqnum));

    /* if there were previous frags, remove from the partialq */
    if (unlikely(!new))
      omx__dequeue_partner_partial_request(partner, req);

    req->generic.state &= ~OMX_REQUEST_STATE_RECV_PARTIAL;
    if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED))
      omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
    else
      omx__recv_complete(ep, req, OMX_SUCCESS);

  } else {
    /* more frags missing */
    omx__debug_printf(MEDIUM, "got one frag of seqnum %d (#%d)\n",
		      (unsigned) OMX__SEQNUM(req->recv.seqnum),
		      (unsigned) OMX__SESNUM_SHIFTED(req->recv.seqnum));

    if (unlikely(new)) {
      req->generic.state |= OMX_REQUEST_STATE_RECV_PARTIAL;
      omx__enqueue_partner_partial_request(partner, req);
    }

    omx__enqueue_request(unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED)
			 ? &ep->ctxid[ctxid].unexp_req_q : &ep->multifrag_medium_recv_req_q,
			 req);
  }
}

void
omx__process_recv_rndv(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       struct omx_evt_recv_msg *msg,
		       void *data /* unused */, uint32_t msg_length)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, msg->match_info);
  struct omx__rndv_data * data_n = (void *) msg->specific.rndv.data;
  uint8_t rdma_id = OMX_FROM_PKT_FIELD(data_n->rdma_id);
  uint8_t rdma_seqnum = OMX_FROM_PKT_FIELD(data_n->rdma_seqnum);
  uint16_t rdma_offset = OMX_FROM_PKT_FIELD(data_n->rdma_offset);

  omx__debug_printf(LARGE, "got a rndv req for rdma id %d seqnum %d offset %d length %d\n",
		    (unsigned) rdma_id, (unsigned) rdma_seqnum, (unsigned) rdma_offset,
		    (unsigned) msg_length);

  req->recv.specific.large.target_rdma_id = rdma_id;
  req->recv.specific.large.target_rdma_seqnum = rdma_seqnum;
  req->recv.specific.large.target_rdma_offset = rdma_offset;

  req->generic.type = OMX_REQUEST_TYPE_RECV_LARGE;
  req->generic.state |= OMX_REQUEST_STATE_RECV_PARTIAL;

  if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED)) {
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
  } else {
    omx__submit_or_queue_pull(ep, req);
  }
}

/*********************************
 * Main packet receive processing
 */

static INLINE void
omx__match_recv(struct omx_endpoint *ep,
		uint64_t match_info,
		union omx_request **reqp)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  union omx_request * req;

  omx__foreach_request(&ep->ctxid[ctxid].recv_req_q, req)
    if (likely(req->recv.match_info == (req->recv.match_mask & match_info))) {
      /* matched a posted recv */
      omx___dequeue_request(req);
      *reqp = req;
      break;
    }
}

static INLINE omx_return_t
omx__try_match_next_recv(struct omx_endpoint *ep,
			 struct omx__partner * partner, omx__seqnum_t seqnum,
			 struct omx_evt_recv_msg *msg, void *data, uint32_t msg_length,
			 omx__process_recv_func_t recv_func)
{
  union omx_request * req = NULL;
  omx_unexp_handler_t handler = ep->unexp_handler;
  omx_endpoint_addr_t source;

  omx__partner_recv_to_addr(partner, &source);

  /* try to match */
  omx__match_recv(ep, msg->match_info, &req);

  /* if no match, try the unexpected handler */
  if (unlikely(handler && !req)) {
    void * handler_context = ep->unexp_handler_context;
    omx_unexp_handler_action_t ret;
    void * data_if_available = NULL;
#ifdef OMX_LIB_DEBUG
    uint64_t omx_handler_jiffies_start;
#endif

    if (likely(msg->type == OMX_EVT_RECV_TINY))
      data_if_available = msg->specific.tiny.data;
    else if (msg->type == OMX_EVT_RECV_SMALL)
      data_if_available = data;

    omx__debug_assert(!(ep->progression_disabled & OMX_PROGRESSION_DISABLED_BY_API));
    omx__debug_assert(!(ep->progression_disabled & OMX_PROGRESSION_DISABLED_IN_HANDLER));
    ep->progression_disabled = OMX_PROGRESSION_DISABLED_IN_HANDLER;
#ifdef OMX_LIB_DEBUG
    omx_handler_jiffies_start = omx__driver_desc->jiffies;
#endif
    OMX__ENDPOINT_UNLOCK(ep);

    ret = handler(handler_context, source, msg->match_info,
		  msg_length, data_if_available);

    OMX__ENDPOINT_LOCK(ep);
    ep->progression_disabled = 0;
    OMX__ENDPOINT_HANDLER_DONE_SIGNAL(ep);
#ifdef OMX_LIB_DEBUG
  {
    uint64_t now = omx__driver_desc->jiffies;
    uint64_t delay = now - omx_handler_jiffies_start;
    if (delay > omx__driver_desc->hz)
      omx__verbose_printf("Unexpected handler disabled progression during %lld seconds (%lld jiffies)\n",
			  (unsigned long long) delay/omx__driver_desc->hz, (unsigned long long) delay);
  }
#endif

    if (ret == OMX_UNEXP_HANDLER_RECV_FINISHED)
      /* the handler took care of the message, we now discard it */
      return OMX_SUCCESS;

    /* if not FINISHED, return MUST be CONTINUE */
    if (ret != OMX_UNEXP_HANDLER_RECV_CONTINUE) {
      omx__abort("The unexpected handler must return either OMX_UNEXP_HANDLER_RECV_FINISHED or _CONTINUE\n");
    }

    /* the unexp has been noticed check if a recv has been posted */
    omx__match_recv(ep, msg->match_info, &req);
  }

  if (likely(req)) {
    /* expected, or matched through the handler */
    uint32_t xfer_length;

    req->generic.partner = partner;
    req->recv.seqnum = seqnum;
    req->generic.status.addr = source;
    req->generic.status.match_info = msg->match_info;

    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_RECV_NEED_MATCHING);
    req->generic.state &= ~OMX_REQUEST_STATE_RECV_NEED_MATCHING;

    req->generic.status.msg_length = msg_length;
    xfer_length = req->recv.segs.total_length < msg_length ? req->recv.segs.total_length : msg_length;
    req->generic.status.xfer_length = xfer_length;

    if (msg->type == OMX_EVT_RECV_MEDIUM)
      omx__init_process_recv_medium(req);

    (*recv_func)(ep, partner, req, msg, data, xfer_length);

  } else {
    /* unexpected, even after the handler */

    req = omx__request_alloc(ep);
    if (unlikely(!req))
      /* let the caller handle the error */
      return OMX_NO_RESOURCES;

    req->generic.type = OMX_REQUEST_TYPE_RECV;
    req->generic.state = OMX_REQUEST_STATE_RECV_UNEXPECTED;

    if (msg->type == OMX_EVT_RECV_MEDIUM)
      omx__init_process_recv_medium(req);

    if (likely(msg->type != OMX_EVT_RECV_RNDV)) {
      /* alloc unexpected buffer, except for rndv since they have no data */
      void *unexp_buffer = NULL;

      if (msg_length) {
	unexp_buffer = malloc(msg_length);
	if (unlikely(!unexp_buffer)) {
	  fprintf(stderr, "Failed to allocate buffer for unexpected messages, dropping\n");
	  omx__request_free(ep, req);
	  /* let the caller handle the error */
	  return OMX_NO_RESOURCES;
	}
      }

      omx_cache_single_segment(&req->recv.segs, unexp_buffer, msg_length);
    }

    req->generic.partner = partner;
    req->recv.seqnum = seqnum;
    req->generic.status.addr = source;
    req->generic.status.match_info = msg->match_info;
    req->generic.status.msg_length = msg_length;

    (*recv_func)(ep, partner, req, msg, data, msg_length);

  }

  return OMX_SUCCESS;
}

static INLINE void
omx__update_partner_next_frag_recv_seq(struct omx_endpoint *ep,
				       struct omx__partner * partner)
{
  omx__seqnum_t old_next_frag_recv_seq = partner->next_frag_recv_seq;
  omx__seqnum_t new_next_frag_recv_seq;

  /* update the seqnum of the next partial fragment to expect
   * if no more partner partial request, we expect a frag for the new seqnum,
   * if not, we expect the fragment for at least the first partial seqnum
   */
  if (omx__empty_partner_partial_queue(partner)) {
    new_next_frag_recv_seq = partner->next_match_recv_seq;
  } else {
    union omx_request *req = omx__first_partner_partial_request(partner);
    new_next_frag_recv_seq = req->recv.seqnum;
  }

  if (new_next_frag_recv_seq != old_next_frag_recv_seq) {

    partner->next_frag_recv_seq = new_next_frag_recv_seq;

    /* if too many non-acked message, ack now */
    if (OMX__SEQNUM(new_next_frag_recv_seq - partner->last_acked_recv_seq) >= omx__globals.not_acked_max) {
      omx__debug_printf(SEQNUM, "seqnums %d-%d (#%d) not acked yet, sending immediate ack\n",
			(unsigned) OMX__SEQNUM(partner->last_acked_recv_seq),
			(unsigned) OMX__SEQNUM(new_next_frag_recv_seq-1),
			(unsigned) OMX__SESNUM_SHIFTED(new_next_frag_recv_seq));

      omx__mark_partner_need_ack_immediate(ep, partner);
    } else {
      omx__mark_partner_need_ack_delayed(ep, partner);
    }
  }
}

static INLINE void
omx__continue_partial_request(struct omx_endpoint *ep,
			      struct omx__partner * partner, omx__seqnum_t seqnum,
			      struct omx_evt_recv_msg *msg, void *data, uint32_t msg_length)
{
  uint64_t match_info = msg->match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  union omx_request * req = NULL;
  omx__seqnum_t new_index = OMX__SEQNUM(seqnum - partner->next_frag_recv_seq);

  omx__foreach_partner_partial_request(partner, req) {
    omx__seqnum_t req_index = OMX__SEQNUM(req->recv.seqnum - partner->next_frag_recv_seq);
    if (likely(req_index == new_index)) {
      omx__dequeue_request(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED
			   ? &ep->ctxid[ctxid].unexp_req_q : &ep->multifrag_medium_recv_req_q,
			   req);
      omx__process_recv_medium_frag(ep, partner, req,
				    msg, data, msg_length);
      omx__update_partner_next_frag_recv_seq(ep, partner);
      return;

    } else if (req_index > new_index) {
      /* just ignore the packet, it could be a duplicate of already completed
       * medium with seqnum higher than a non-completed medium
       */
      return;
    }
  }

}

static INLINE omx_return_t
omx__process_partner_ordered_recv(struct omx_endpoint *ep,
				  struct omx__partner *partner, omx__seqnum_t seqnum,
				  struct omx_evt_recv_msg *msg, void *data, uint32_t msg_length,
				  omx__process_recv_func_t recv_func)
{
  omx_return_t ret = OMX_SUCCESS;
  omx__seqnum_t match_index = OMX__SEQNUM(seqnum - partner->next_match_recv_seq);
  omx__seqnum_t frag_index = OMX__SEQNUM(seqnum - partner->next_frag_recv_seq);
  omx__seqnum_t frag_index_max = OMX__SEQNUM(partner->next_match_recv_seq - partner->next_frag_recv_seq);

  if (likely(match_index == 0)) {
    /* expected seqnum */

    if (unlikely(msg->type == OMX_EVT_RECV_NOTIFY)) {
      /* internal message, no matching to do, just a recv+seqnum to handle */
      (*recv_func)(ep, partner, NULL, msg, NULL, 0);
    } else {
      /* regular message, do the matching */
      ret = omx__try_match_next_recv(ep, partner, seqnum,
				     msg, data, msg_length,
				     recv_func);
      /* ignore errors, the packet will be resent anyway */
    }

    if (ret == OMX_SUCCESS) {
      /* we matched this seqnum, we now expect the next one */
      OMX__SEQNUM_INCREASE(partner->next_match_recv_seq);
      omx__update_partner_next_frag_recv_seq(ep, partner);
    }

  } else if (likely(msg->type == OMX_EVT_RECV_MEDIUM
		    && frag_index < frag_index_max)) {
    /* fragment of already matched but incomplete medium message */
    omx__continue_partial_request(ep, partner, seqnum,
				  msg, data, msg_length);

  } else {
    /* obsolete fragment or message, just ignore it */
  }

  return ret;
}

void
omx__process_recv(struct omx_endpoint *ep,
		  struct omx_evt_recv_msg *msg, void *data, uint32_t msg_length,
		  omx__process_recv_func_t recv_func)
{
  omx__seqnum_t seqnum = msg->seqnum;
  omx__seqnum_t piggyack = msg->piggyack;
  struct omx__partner * partner;
  omx__seqnum_t old_next_match_recv_seq;
  omx__seqnum_t frag_index;
  omx__seqnum_t frag_index_max;

  omx__partner_recv_lookup(ep, msg->peer_index, msg->src_endpoint,
			   &partner);
  if (unlikely(!partner))
    return;

  omx__debug_printf(SEQNUM, "got seqnum %d (#%d), expected match at %d, frag at %d (#%d)\n",
		    (unsigned) OMX__SEQNUM(seqnum),
		    (unsigned) OMX__SESNUM_SHIFTED(seqnum),
		    (unsigned) OMX__SEQNUM(partner->next_match_recv_seq),
		    (unsigned) OMX__SEQNUM(partner->next_frag_recv_seq),
		    (unsigned) OMX__SESNUM_SHIFTED(partner->next_frag_recv_seq));

  if (unlikely(OMX__SESNUM(seqnum ^ partner->next_frag_recv_seq)) != 0) {
    omx__verbose_printf("Obsolete session message received (session %d seqnum %d instead of session %d)\n",
			(unsigned) OMX__SESNUM_SHIFTED(seqnum), (unsigned) OMX__SEQNUM(seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(partner->next_frag_recv_seq));
    return;
  }

  if (unlikely(OMX__SESNUM(piggyack ^ partner->next_send_seq)) != 0) {
    omx__verbose_printf("Obsolete session piggyack received (session %d seqnum %d instead of session %d)\n",
			(unsigned) OMX__SESNUM_SHIFTED(piggyack), (unsigned) OMX__SEQNUM(piggyack),
			(unsigned) OMX__SESNUM_SHIFTED(partner->next_send_seq));
    return;
  }

  omx__debug_printf(ACK, "got piggy ack for ack up to %d (#%d)\n",
		    (unsigned) OMX__SEQNUM(piggyack - 1),
		    (unsigned) OMX__SESNUM_SHIFTED(piggyack - 1));
  omx__handle_ack(ep, partner, piggyack);

  old_next_match_recv_seq = partner->next_match_recv_seq;
  frag_index = OMX__SEQNUM(seqnum - partner->next_frag_recv_seq);
  frag_index_max = OMX__SEQNUM(old_next_match_recv_seq - partner->next_frag_recv_seq);

  if (likely(frag_index <= frag_index_max)) {
    omx_return_t ret;

    /* either the new expected seqnum (to match)
     * or a incomplete previous multi-fragment medium messages (to accumulate)
     * or an old obsolete duplicate packet (to drop)
     */
    ret = omx__process_partner_ordered_recv(ep, partner, seqnum,
					    msg, data, msg_length,
					    recv_func);
    /* ignore errors, the packet will be resent anyway, the recv seqnums didn't increase */

    /* process early packets in case they match the new expected seqnum */
    if (likely(old_next_match_recv_seq != partner->next_match_recv_seq)) {
      omx__seqnum_t early_index_max = OMX__SEQNUM(partner->next_match_recv_seq - old_next_match_recv_seq);
      struct omx__early_packet * early, * next;
      omx__foreach_partner_early_packet_safe(partner, early, next) {
	omx__seqnum_t early_index = OMX__SEQNUM(early->msg.seqnum - old_next_match_recv_seq);
	if (early_index <= early_index_max) {
	  omx___dequeue_partner_early_packet(early);
	  omx__debug_printf(EARLY, "processing early packet with seqnum %d (#%d)\n",
			    (unsigned) OMX__SEQNUM(early->msg.seqnum),
			    (unsigned) OMX__SESNUM_SHIFTED(early->msg.seqnum));

	  ret = omx__process_partner_ordered_recv(ep, partner, early->msg.seqnum,
						  &early->msg, early->data, early->msg_length,
						  early->recv_func);
	  /* ignore errors, the packet will be resent anyway, the recv seqnums didn't increase */

	  if (early->data)
	    free(early->data);
	  free(early);
	}
      }
    }

  } else if (frag_index <= frag_index_max + OMX__EARLY_PACKET_OFFSET_MAX) {
    /* early fragment or message, postpone it */
    omx__postpone_early_packet(partner,
			       msg, data,
			       recv_func);

  } else {
    omx__debug_printf(SEQNUM, "obsolete message %d (#%d), assume a ack has been lost\n",
		      (unsigned) OMX__SEQNUM(seqnum),
		      (unsigned) OMX__SESNUM_SHIFTED(seqnum));

    if (frag_index == OMX__SEQNUM(-1)) {
      /* assume a ack has been lost, resend a ack now, but only if
       * the obsolete is the previous packet (so that we don't flood the peer with acks)
       */
      omx__mark_partner_need_ack_immediate(ep, partner);
    }
  }
}

/******************************
 * Receive Message from Myself
 */

void
omx__process_self_send(struct omx_endpoint *ep,
		       union omx_request *sreq)
{
  union omx_request * rreq = NULL;
  omx_unexp_handler_t handler = ep->unexp_handler;
  uint64_t match_info = sreq->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  uint32_t msg_length = sreq->send.segs.total_length;
  omx_return_t status_code;

  sreq->generic.type = OMX_REQUEST_TYPE_SEND_SELF;
  sreq->generic.partner = ep->myself;
  sreq->generic.status.match_info = match_info;
  sreq->generic.status.msg_length = msg_length;
  /* xfer_length will be set on matching */

  /* try to match */
  omx__match_recv(ep, match_info, &rreq);

  /* if no match, try the unexpected handler */
  if (unlikely(handler && !rreq)) {
    void * handler_context = ep->unexp_handler_context;
    omx_unexp_handler_action_t ret;
    void * data_if_available;
#ifdef OMX_LIB_DEBUG
    uint64_t omx_handler_jiffies_start;
#endif

    if (likely(sreq->send.segs.nseg == 1))
      data_if_available = sreq->send.segs.single.ptr;
    else
      data_if_available = NULL; /* FIXME: copy in a linear buffer first */

    omx__debug_assert(!(ep->progression_disabled & OMX_PROGRESSION_DISABLED_BY_API));
    omx__debug_assert(!(ep->progression_disabled & OMX_PROGRESSION_DISABLED_IN_HANDLER));
    ep->progression_disabled = OMX_PROGRESSION_DISABLED_IN_HANDLER;
#ifdef OMX_LIB_DEBUG
    omx_handler_jiffies_start = omx__driver_desc->jiffies;
#endif
    OMX__ENDPOINT_UNLOCK(ep);

    ret = handler(handler_context, sreq->generic.status.addr, match_info,
		  msg_length, data_if_available);

    OMX__ENDPOINT_LOCK(ep);
    ep->progression_disabled = 0;
    OMX__ENDPOINT_HANDLER_DONE_SIGNAL(ep);
#ifdef OMX_LIB_DEBUG
  {
    uint64_t now = omx__driver_desc->jiffies;
    uint64_t delay = now - omx_handler_jiffies_start;
    if (delay > omx__driver_desc->hz)
      omx__verbose_printf("Unexpected handler disabled progression during %lld seconds (%lld jiffies)\n",
			  (unsigned long long) delay/omx__driver_desc->hz, (unsigned long long) delay);
  }
#endif

    if (ret == OMX_UNEXP_HANDLER_RECV_FINISHED) {
      /* the handler took care of the message, just complete the send request */
      sreq->generic.status.xfer_length = msg_length;
      omx__send_complete(ep, sreq, OMX_SUCCESS);
      return;
    }

    /* if not FINISHED, return MUST be CONTINUE */
    if (ret != OMX_UNEXP_HANDLER_RECV_CONTINUE) {
      omx__abort("The unexpected handler must return either OMX_UNEXP_HANDLER_RECV_FINISHED and _CONTINUE\n");
    }

    /* the unexp has been noticed check if a recv has been posted */
    omx__match_recv(ep, match_info, &rreq);
  }

  if (likely(rreq)) {
    /* expected, or matched through the handler */
    uint32_t xfer_length;
    omx_return_t status_code;

    rreq->generic.partner = ep->myself;
    rreq->generic.status.addr = sreq->generic.status.addr;
    rreq->generic.status.match_info = match_info;

    omx__debug_assert(rreq->generic.state & OMX_REQUEST_STATE_RECV_NEED_MATCHING);
    rreq->generic.state &= ~OMX_REQUEST_STATE_RECV_NEED_MATCHING;

    rreq->generic.status.msg_length = msg_length;
    if (rreq->recv.segs.total_length < msg_length) {
      xfer_length = rreq->recv.segs.total_length;
      status_code = OMX_MESSAGE_TRUNCATED;
    } else {
      status_code = OMX_SUCCESS;
      xfer_length = msg_length;
    }
    rreq->generic.status.xfer_length = xfer_length;
    sreq->generic.status.xfer_length = xfer_length;

    omx_copy_from_to_segments(&rreq->recv.segs, &sreq->send.segs, xfer_length);
    omx__send_complete(ep, sreq, status_code);
    omx__recv_complete(ep, rreq, status_code);

    /*
     * need to wakeup some possible send-done or recv-done waiters
     * since this event does not come from the driver
     */
    omx__notify_user_event(ep);

  } else {
    /* unexpected, even after the handler */
    void *unexp_buffer = NULL;

    rreq = omx__request_alloc(ep);
    if (unlikely(!rreq)) {
      status_code = omx__error_with_ep(ep, OMX_NO_RESOURCES,
				       "Allocating unexpected receive for self send");
      goto failed;
    }

    if (msg_length) {
      unexp_buffer = malloc(msg_length);
      if (unlikely(!unexp_buffer)) {
	omx__request_free(ep, rreq);
	status_code = omx__error_with_ep(ep, OMX_NO_RESOURCES,
					 "Allocating unexpected buffer for self send");
	goto failed;
      }
    }

    rreq->generic.type = OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED;
    rreq->generic.state = OMX_REQUEST_STATE_RECV_UNEXPECTED;

    omx_cache_single_segment(&rreq->recv.segs, unexp_buffer, msg_length);

    rreq->generic.partner = ep->myself;
    rreq->generic.status.addr = sreq->generic.status.addr;
    rreq->generic.status.match_info = match_info;
    rreq->generic.status.msg_length = msg_length;

    rreq->recv.specific.self_unexp.sreq = sreq;
    omx_copy_from_segments(unexp_buffer, &sreq->send.segs, msg_length);
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, rreq);

    /* self communication are always synchronous,
     * the send will be completed on matching
     */
    sreq->generic.state |= OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED;
    omx__enqueue_request(&ep->send_self_unexp_req_q, sreq);

  }

  return;

 failed:
  /*
   * queueing would be a mess. and there's no connection/seqnums to break
   * here if the message isn't received. just complete with an error.
   */
  sreq->generic.state = 0; /* reset the state before completion */
  omx__send_complete(ep, sreq, status_code);

  /*
   * need to wakeup some possible send-done waiters
   * since this event does not come from the driver
   */
  omx__notify_user_event(ep);
}

/***********************
 * Truc Message Receive
 */

void
omx__process_recv_truc(struct omx_endpoint *ep,
		       struct omx_evt_recv_truc *truc)
{
  union omx__truc_data *data_n = (void *) truc->data;
  uint8_t truc_type = OMX_FROM_PKT_FIELD(data_n->type);
  struct omx__partner *partner;

  omx__partner_recv_lookup(ep, truc->peer_index, truc->src_endpoint,
			   &partner);
  if (unlikely(!partner))
    return;

  switch (truc_type) {
  case OMX__TRUC_DATA_TYPE_ACK: {
    omx__handle_truc_ack(ep, partner, &data_n->ack);
    break;
  }
  default:
    omx__abort("Failed to handle truc message with type %d\n", truc_type);
  }
}

/***************************
 * Nack Lib Message Receive
 */

void
omx__process_recv_nack_lib(struct omx_endpoint *ep,
			   struct omx_evt_recv_nack_lib *nack_lib)
{
  uint16_t peer_index = nack_lib->peer_index;
  uint16_t seqnum = nack_lib->seqnum;
  uint8_t nack_type = nack_lib->nack_type;
  struct omx__partner * partner;
  uint64_t board_addr = 0;
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  omx_return_t status;
  omx_return_t ret;

  omx__partner_recv_lookup(ep, peer_index, nack_lib->src_endpoint,
			   &partner);
  if (unlikely(!partner))
    return;

  ret = omx__peer_index_to_addr(peer_index, &board_addr);
  /* if the partner exists, the peer has to exist too */
  omx__debug_assert(ret == OMX_SUCCESS);

  omx__board_addr_sprintf(board_addr_str, board_addr);

  switch (nack_type) {
  case OMX_EVT_NACK_LIB_BAD_ENDPT:
    status = OMX_REMOTE_ENDPOINT_BAD_ID;
    break;
  case OMX_EVT_NACK_LIB_ENDPT_CLOSED:
    status = OMX_REMOTE_ENDPOINT_CLOSED;
    break;
  case OMX_EVT_NACK_LIB_BAD_SESSION:
    status = OMX_REMOTE_ENDPOINT_BAD_SESSION;
    break;
  default:
    omx__abort("Failed to handle NACK with unknown type (%d) from peer %s (index %d) seqnum %d (#%d)\n",
	       (unsigned) nack_type, board_addr_str, (unsigned) peer_index,
	       (unsigned) OMX__SEQNUM(seqnum),
	       (unsigned) OMX__SESNUM_SHIFTED(seqnum));
  }

  omx__handle_nack(ep, partner, seqnum, status);
}

/**************************
 * Main IRECV and IRECVV routines
 */

static INLINE omx_return_t
omx__irecv_segs(struct omx_endpoint *ep, struct omx__req_seg * reqsegs,
		uint64_t match_info, uint64_t match_mask,
		void *context, union omx_request **requestp)
{
  union omx_request * req;
  omx_return_t ret;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  uint32_t msg_length;
  uint32_t xfer_length;

  omx__foreach_request(&ep->ctxid[ctxid].unexp_req_q, req) {
    if (likely((req->generic.status.match_info & match_mask) == match_info)) {
      /* matched an unexpected */
      void * unexp_buffer;

      /* get the unexp buffer and store the new segments */
      unexp_buffer = req->recv.segs.single.ptr;
      memcpy(&req->recv.segs, reqsegs, sizeof(*reqsegs));

      omx___dequeue_request(req);

      /* compute xfer_length */
      msg_length = req->generic.status.msg_length;
      xfer_length = req->recv.segs.total_length < msg_length ? req->recv.segs.total_length : msg_length;
      req->generic.status.xfer_length = xfer_length;

      omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED);
      req->generic.state &= ~OMX_REQUEST_STATE_RECV_UNEXPECTED;

      req->generic.status.context = context;

      if (unlikely(req->generic.type == OMX_REQUEST_TYPE_RECV_LARGE)) {
	/* it's a large message, queue the recv large */
	omx__submit_or_queue_pull(ep, req);

      } else if (unlikely(req->generic.type == OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED)) {
	/* it's a unexpected from self, we need to complete the corresponding send */
	union omx_request *sreq = req->recv.specific.self_unexp.sreq;
	omx_return_t status_code = xfer_length < msg_length ? OMX_MESSAGE_TRUNCATED : OMX_SUCCESS;

	omx_copy_to_segments(reqsegs, unexp_buffer, xfer_length);
	if (msg_length)
	  free(unexp_buffer);
	omx__recv_complete(ep, req, status_code);

	omx__debug_assert(sreq->generic.state & OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED);
	sreq->generic.state &= ~OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED;
	omx__dequeue_request(&ep->send_self_unexp_req_q, sreq);
	sreq->generic.status.xfer_length = xfer_length;
	omx__send_complete(ep, sreq, status_code);

	/*
	 * need to wakeup some possible send-done or recv-done waiters
	 * since this event does not come from the driver
	 */
	omx__notify_user_event(ep);

      } else {
	/* it's a tiny/small/medium, copy the data back to our buffer */

	omx_copy_to_segments(reqsegs, unexp_buffer, xfer_length); /* FIXME: could just copy what has been received */
	if (msg_length)
	  free(unexp_buffer);

	if (unlikely(req->generic.state)) {
	  omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL);
	  /* no need to reset the scan_state, the unexpected buffer didn't use it since it's contigous */
	  omx__enqueue_request(&ep->multifrag_medium_recv_req_q, req);
	} else {
	  omx__recv_complete(ep, req, OMX_SUCCESS);

	  /*
	   * need to wakeup some possible recv-done waiters
	   * since this event does not come from the driver
	   */
	  omx__notify_user_event(ep);
	}
      }

      goto ok;
    }
  }

  /* allocate a new recv request */
  req = omx__request_alloc(ep);
  if (unlikely(!req)) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating irecv request");
    goto out;
  }

  memcpy(&req->recv.segs, reqsegs, sizeof(*reqsegs));

  req->generic.type = OMX_REQUEST_TYPE_RECV;
  req->generic.state = OMX_REQUEST_STATE_RECV_NEED_MATCHING;
  req->generic.status.context = context;
  req->recv.match_info = match_info;
  req->recv.match_mask = match_mask;

  omx__enqueue_request(&ep->ctxid[ctxid].recv_req_q, req);
  omx__progress(ep);

 ok:
  if (requestp) {
    *requestp = req;
  } else {
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
    ep->zombies++;
  }

  return OMX_SUCCESS;

 out:
  return ret;
}

/* API omx_irecv */
omx_return_t
omx_irecv(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, union omx_request **requestp)
{
  struct omx__req_seg reqsegs;
  omx_return_t ret;

  if (unlikely(match_info & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCH_MASK,
			     "irecv with match info %llx mask %llx",
			     (unsigned long long) match_info, (unsigned long long) match_mask);
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(ep->ctxid_mask & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK,
			     "irecv with match mask %llx and ctxid mask %llx",
			     (unsigned long long) match_mask, ep->ctxid_mask);
    goto out;
  }

  omx_cache_single_segment(&reqsegs, buffer, length);

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__irecv_segs(ep, &reqsegs, match_info, match_mask, context, requestp);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  OMX__ENDPOINT_UNLOCK(ep);
  return OMX_SUCCESS;

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  omx_free_segments(&reqsegs);
 out:
  return ret;
}

/* API omx_irecvv */
omx_return_t
omx_irecvv(omx_endpoint_t ep,
	   omx_seg_t *segs, uint32_t nseg,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, omx_request_t * requestp)
{
  struct omx__req_seg reqsegs;
  omx_return_t ret;

  if (unlikely(match_info & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCH_MASK,
			     "irecvv with match info %llx mask %llx",
			     (unsigned long long) match_info, (unsigned long long) match_mask);
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(ep->ctxid_mask & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK,
			     "irecvv with match mask %llx and ctxid mask %llx",
			     (unsigned long long) match_mask, ep->ctxid_mask);
    goto out;
  }

  ret = omx_cache_segments(&reqsegs, segs, nseg);
  if (unlikely(ret != OMX_SUCCESS)) {
    /* the callee let us check errors */
    ret = omx__error_with_ep(ep, ret,
			     "Allocating %ld-vectorial receive request segment array",
			     (unsigned long long) nseg);
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__irecv_segs(ep, &reqsegs, match_info, match_mask, context, requestp);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  OMX__ENDPOINT_UNLOCK(ep);
  return OMX_SUCCESS;

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  omx_free_segments(&reqsegs);
 out:
  return ret;
}

