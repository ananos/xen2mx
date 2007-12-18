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

#include "omx_lib.h"
#include "omx_request.h"
#include "omx_wire_access.h"
#include "omx_lib_wire.h"

/*********************
 * Receive completion
 */

void
omx__recv_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_status_code_t status)
{
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);

  if (likely(req->generic.status.code == OMX_STATUS_SUCCESS)) {
    /* only set the status if it is not already set to an error */
    if (likely(status == OMX_STATUS_SUCCESS)) {
      if (unlikely(req->generic.status.xfer_length < req->generic.status.msg_length))
	req->generic.status.code = OMX_STATUS_TRUNCATED;
    } else {
      req->generic.status.code = status;
    }
  }

  omx__notify_request_done(ep, ctxid, req);
}

/****************
 * Early packets
 */

/* find which early which need to queue the new one after,
 * or drop if duplicate
 */
static INLINE struct list_head *
omx__find_previous_early(struct omx__partner * partner,
			 struct omx_evt_recv_msg *msg)
{
  omx__seqnum_t seqnum = msg->seqnum;
  omx__seqnum_t next_match_recv_seq = partner->next_match_recv_seq;
  struct omx__early_packet * current;
  omx__seqnum_t new_index;
  omx__seqnum_t current_index;

  /* trivial case, early queue is empty */
  if (list_empty(&partner->early_recv_q)) {
    omx__debug_printf("insert early in empty queue\n");
    return &partner->early_recv_q;
  }

  new_index = OMX__SEQNUM(seqnum - next_match_recv_seq);

  /* a little bit less trivial case, append at the end */
  current = omx__partner_last_early_packet(partner);
  current_index = OMX__SEQNUM(current->msg.seqnum - next_match_recv_seq);
  if (new_index > current_index) {
    omx__debug_printf("inserting early at the end of queue\n");
    return partner->early_recv_q.prev;
  }

  /* a little bit less trivial case, append at the beginning */
  current = omx__partner_first_early_packet(partner);
  current_index = OMX__SEQNUM(current->msg.seqnum - next_match_recv_seq);
  if (new_index < current_index) {
    omx__debug_printf("inserting early at the beginning of queue\n");
    return &partner->early_recv_q;
  }

  /* general case, add at the right position, and drop if duplicate */
  list_for_each_entry_reverse(current, &partner->early_recv_q, partner_elt) {
    current_index = OMX__SEQNUM(current->msg.seqnum - next_match_recv_seq);

    if (new_index > current_index) {
      /* found an earlier one, insert after it */
      omx__debug_printf("inserting early after another one\n");
      return &current->partner_elt;
    }

    if (new_index < current_index) {
      /* later one, look further */
      omx__debug_printf("not inserting early after this one\n");
      continue;
    }

    if (msg->type == OMX_EVT_RECV_MEDIUM) {
      /* medium early, check the frag num */
      unsigned long current_frag_seqnum = current->msg.specific.medium.frag_seqnum;
      unsigned long new_frag_seqnum = msg->specific.medium.frag_seqnum;

      if (new_frag_seqnum > current_frag_seqnum) {
	/* found an earlier one, insert after it */
	omx__debug_printf("inserting early after this medium\n");
	return &current->partner_elt;
      }

      if (new_frag_seqnum < current_frag_seqnum) {
	/* later one, look further */
	omx__debug_printf("not inserting early after this medium\n");
	continue;
      }

      /* that's a duplicate medium frag, drop it */
      omx__debug_printf("dropping duplicate early medium\n");
      return NULL;
    }

    /* that's a duplicate, drop it */
    omx__debug_printf("dropping duplicate early\n");
    return NULL;
  }

  omx__abort("Found no previous early");
}

static INLINE omx_return_t
omx__postpone_early_packet(struct omx__partner * partner,
			   struct omx_evt_recv_msg *msg, void *data,
			   omx__process_recv_func_t recv_func)
{
  struct omx__early_packet * early;
  struct list_head * prev;

  prev = omx__find_previous_early(partner, msg);
  if (!prev)
    return OMX_SUCCESS;

  early = malloc(sizeof(*early));
  if (unlikely(!early))
    return OMX_NO_RESOURCES;

  /* copy the whole event, the callback, and the data */
  memcpy(&early->msg, msg, sizeof(*msg));
  early->recv_func = recv_func;

  switch (msg->type) {
  case OMX_EVT_RECV_TINY:
    /* no need to set early->data, omx__process_recv_tiny
     * always takes the data from inside the event
     */
    early->data = NULL;
    early->msg_length = msg->specific.tiny.length;
    break;

  case OMX_EVT_RECV_SMALL: {
    uint16_t length = msg->specific.small.length;
    char * early_data = malloc(length);
    if (!early_data) {
      free(early);
      return OMX_NO_RESOURCES;
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
      return OMX_NO_RESOURCES;
    }
    memcpy(early_data, data, frag_length);
    early->data = early_data;
    early->msg_length = msg->specific.medium.msg_length;
    break;
  }

  case OMX_EVT_RECV_RNDV: {
    struct omx__rndv_data * data_n = (void *) msg->specific.rndv.data;
    uint32_t msg_length = OMX_FROM_PKT_FIELD(data_n->msg_length);
    early->data = NULL;
    early->msg_length = msg_length;
    break;
  }

  case OMX_EVT_RECV_NOTIFY: {
    break;
  }

  default:
    omx__abort("Failed to handle early packet with type %d\n",
	       msg->type);
  }

  omx__debug_printf("postponing early packet with seqnum %d\n",
		    msg->seqnum);

  list_add(&early->partner_elt, prev);

  return OMX_SUCCESS;
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

  memcpy(req->recv.buffer, msg->specific.tiny.data, msg_length);

  if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED))
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
  else
    omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);
}

void
omx__process_recv_small(struct omx_endpoint *ep, struct omx__partner *partner,
			union omx_request *req,
			struct omx_evt_recv_msg *msg,
			void *data, uint32_t msg_length)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, msg->match_info);

  memcpy(req->recv.buffer, data, msg_length);

  if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED))
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
  else
    omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);
}

static inline void
omx__init_process_recv_medium(union omx_request *req)
{
  req->recv.specific.medium.frags_received_mask = 0;
  req->recv.specific.medium.accumulated_length = 0;
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

  omx__debug_printf("got a medium frag seqnum %d pipeline %d length %d offset %d of total %d\n",
		    (unsigned) frag_seqnum, (unsigned) frag_pipeline, (unsigned) chunk,
		    (unsigned) offset, (unsigned) msg_length);

  if (unlikely(req->recv.specific.medium.frags_received_mask & (1 << frag_seqnum))) {
    /* already received this frag, requeue back */
    omx__debug_printf("got a duplicate frag seqnum %d for medium seqnum %d\n",
		      (unsigned) frag_seqnum, (unsigned) req->recv.seqnum);
    omx__enqueue_request(unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED)
			 ? &ep->ctxid[ctxid].unexp_req_q : &ep->multifrag_medium_recv_req_q,
			 req);
    return;
  }

  /* take care of the data chunk */
  if (unlikely(offset + chunk > msg_length))
    chunk = msg_length - offset;
  memcpy(req->recv.buffer + offset, data, chunk);
  req->recv.specific.medium.frags_received_mask |= 1 << frag_seqnum;
  req->recv.specific.medium.accumulated_length += chunk;

  if (likely(req->recv.specific.medium.accumulated_length == msg_length)) {
    /* was the last frag */
    omx__debug_printf("got last frag of seqnum %d\n", req->recv.seqnum);

    /* if there were previous frags, remove from the partialq */
    if (unlikely(!new))
      omx__dequeue_partner_partial_request(partner, req);

    req->generic.state &= ~OMX_REQUEST_STATE_RECV_PARTIAL;
    if (unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED))
      omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, req);
    else
      omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);

  } else {
    /* more frags missing */
    omx__debug_printf("got one frag of seqnum %d\n", req->recv.seqnum);

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

  omx__debug_printf("got a rndv req for rdma id %d seqnum %d offset %d length %d\n",
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
      omx__dequeue_request(&ep->ctxid[ctxid].recv_req_q, req);
      *reqp = req;
      return;
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

  /* try to match */
  omx__match_recv(ep, msg->match_info, &req);

  /* if no match, try the unexpected handler */
  if (unlikely(handler && !req)) {
    void * context = ep->unexp_handler_context;
    omx_unexp_handler_action_t ret;
    omx_endpoint_addr_t source;
    void * data_if_available = NULL;

    if (likely(msg->type == OMX_EVT_RECV_TINY))
      data_if_available = msg->specific.tiny.data;
    else if (msg->type == OMX_EVT_RECV_SMALL)
      data_if_available = data;

    omx__partner_to_addr(partner, &source);
    ep->in_handler = 1;
    /* FIXME: lock */
    ret = handler(context, source, msg->match_info,
		  msg_length, data_if_available);
    /* FIXME: unlock */
    ep->in_handler = 0;
    /* FIXME: signal */
    if (ret == OMX_RECV_FINISHED)
      /* the handler took care of the message, we now discard it */
      return OMX_SUCCESS;

    /* if not FINISHED, return MUST be CONTINUE */
    if (ret == OMX_RECV_CONTINUE) {
      omx__abort("The unexpected handler must return either OMX_RECV_FINISHED and OMX_RECV_CONTINUE\n");
    }

    /* the unexp has been noticed check if a recv has been posted */
    omx__match_recv(ep, msg->match_info, &req);
  }

  if (likely(req)) {
    /* expected, or matched through the handler */
    uint32_t xfer_length;

    req->generic.partner = partner;
    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->recv.seqnum = seqnum;
    req->generic.status.match_info = msg->match_info;

    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_RECV_NEED_MATCHING);
    req->generic.state &= ~OMX_REQUEST_STATE_RECV_NEED_MATCHING;

    req->generic.status.msg_length = msg_length;
    xfer_length = req->recv.length < msg_length ? req->recv.length : msg_length;
    req->generic.status.xfer_length = xfer_length;

    if (msg->type == OMX_EVT_RECV_MEDIUM)
      omx__init_process_recv_medium(req);

    (*recv_func)(ep, partner, req, msg, data, xfer_length);

  } else {
    /* unexpected, even after the handler */

    req = omx__request_alloc(ep);
    if (unlikely(!req))
      return OMX_NO_RESOURCES;

    req->generic.type = OMX_REQUEST_TYPE_RECV;

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
	  return OMX_NO_RESOURCES;
	}
      }

      req->recv.buffer = unexp_buffer;
    }

    req->generic.partner = partner;
    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->recv.seqnum = seqnum;
    req->generic.status.match_info = msg->match_info;
    req->generic.state = OMX_REQUEST_STATE_RECV_UNEXPECTED; /* the state of unexpected recv is always set here */

    req->generic.status.msg_length = msg_length;

    (*recv_func)(ep, partner, req, msg, data, msg_length);

  }

  return OMX_SUCCESS;
}

static INLINE void
omx__update_partner_next_frag_recv_seq(struct omx_endpoint *ep,
				       struct omx__partner * partner)
{
  /* update the seqnum of the next partial fragment to expect
   * if no more partner partial request, we expect a frag for the new seqnum,
   * if not, we expect the fragment for at least the first partial seqnum
   */
  if (omx__partner_partial_queue_empty(partner)) {
    partner->next_frag_recv_seq = partner->next_match_recv_seq;
  } else {
    union omx_request *req = omx__partner_partial_queue_first_request(partner);
    partner->next_frag_recv_seq = req->recv.seqnum;
  }
}

static INLINE omx_return_t
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
      omx__partner_needs_to_ack(ep, partner);
      return OMX_SUCCESS;
    } else if (req_index > new_index) {
      break;
    }
  }

  /* just ignore the packet, it can be a duplicate of already completed
   * medium with seqnum higher than a non-completed medium
   */
  return OMX_SUCCESS;
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
    }

    if (ret == OMX_SUCCESS) {
      /* we matched this seqnum, we now expect the next one */
      partner->next_match_recv_seq = OMX__SEQNUM(partner->next_match_recv_seq + 1);
      omx__update_partner_next_frag_recv_seq(ep, partner);
      omx__partner_needs_to_ack(ep, partner);
    }

  } else if (likely(msg->type == OMX_EVT_RECV_MEDIUM
		    && frag_index < frag_index_max)) {
    /* fragment of already matched but incomplete medium message */
    ret = omx__continue_partial_request(ep, partner, seqnum,
					msg, data, msg_length);

  } else {
    /* obsolete fragment or message, just ignore it */
    ret = OMX_SUCCESS;
  }

  return ret;
}

omx_return_t
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
  omx_return_t ret;

  ret = omx__partner_recv_lookup(ep, msg->peer_index, msg->src_endpoint,
				 &partner);
  if (unlikely(ret != OMX_SUCCESS))
    return ret;

  omx__debug_printf("got seqnum %d, expected match at %d, frag at %d\n",
		    seqnum, partner->next_match_recv_seq, partner->next_frag_recv_seq);

  omx__handle_ack(ep, partner, piggyack);

  old_next_match_recv_seq = partner->next_match_recv_seq;
  frag_index = OMX__SEQNUM(seqnum - partner->next_frag_recv_seq);
  frag_index_max = OMX__SEQNUM(old_next_match_recv_seq - partner->next_frag_recv_seq);

  if (likely(frag_index <= frag_index_max)) {
    /* either the new expected seqnum (to match)
     * or a incomplete previous multi-fragment medium messages (to accumulate)
     * or an old obsolete duplicate packet (to drop)
     */
    ret = omx__process_partner_ordered_recv(ep, partner, seqnum,
					    msg, data, msg_length,
					    recv_func);

    /* process early packets in case they match the new expected seqnum */
    if (likely(old_next_match_recv_seq != partner->next_match_recv_seq)) {
      omx__seqnum_t early_index_max = OMX__SEQNUM(partner->next_match_recv_seq - old_next_match_recv_seq);
      struct omx__early_packet * early, * next;
      omx__foreach_partner_early_packet_safe(partner, early, next) {
	omx__seqnum_t early_index = OMX__SEQNUM(early->msg.seqnum - old_next_match_recv_seq);
	if (early_index <= early_index_max) {
	  omx__dequeue_partner_early_packet(partner, early);
	  omx__debug_printf("processing early packet with seqnum %d\n",
			    early->msg.seqnum);
	  ret = omx__process_partner_ordered_recv(ep, partner, early->msg.seqnum,
						  &early->msg, early->data, early->msg_length,
						  early->recv_func);
	  if (early->data)
	    free(early->data);
	  free(early);
	}
      }
    }

  } else {
    /* early fragment or message, postpone it */
    ret = omx__postpone_early_packet(partner,
				     msg, data,
				     recv_func);

  }

  return ret;
}

/******************************
 * Receive Message from Myself
 */

omx_return_t
omx__process_self_send(struct omx_endpoint *ep,
		       union omx_request *sreq,
		       void *sbuffer, size_t msg_length,
		       uint64_t match_info,
		       void *context)
{
  union omx_request * rreq = NULL;
  omx_unexp_handler_t handler = ep->unexp_handler;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);

  sreq->generic.type = OMX_REQUEST_TYPE_SEND_SELF;
  sreq->generic.partner = ep->myself;
  omx__partner_to_addr(ep->myself, &sreq->generic.status.addr);
  sreq->generic.status.context = context;
  sreq->generic.status.match_info = match_info;
  sreq->generic.status.msg_length = msg_length;
  /* xfer_length will be set on matching */

  /* try to match */
  omx__match_recv(ep, match_info, &rreq);

  /* if no match, try the unexpected handler */
  if (unlikely(handler && !rreq)) {
    void * context = ep->unexp_handler_context;
    omx_unexp_handler_action_t ret;
    void * data_if_available = sbuffer;

    ep->in_handler = 1;
    /* FIXME: lock */
    ret = handler(context, sreq->generic.status.addr, match_info,
		  msg_length, data_if_available);
    /* FIXME: unlock */
    ep->in_handler = 0;
    /* FIXME: signal */
    if (ret == OMX_RECV_FINISHED) {
      /* the handler took care of the message, just complete the send request */
      sreq->generic.status.xfer_length = msg_length;
      omx__send_complete(ep, sreq, OMX_STATUS_SUCCESS);
      return OMX_SUCCESS;
    }

    /* if not FINISHED, return MUST be CONTINUE */
    if (ret == OMX_RECV_CONTINUE) {
      omx__abort("The unexpected handler must return either OMX_RECV_FINISHED and OMX_RECV_CONTINUE\n");
    }

    /* the unexp has been noticed check if a recv has been posted */
    omx__match_recv(ep, match_info, &rreq);
  }

  if (likely(rreq)) {
    /* expected, or matched through the handler */
    uint32_t xfer_length;

    rreq->generic.partner = ep->myself;
    omx__partner_to_addr(ep->myself, &rreq->generic.status.addr);
    rreq->generic.status.match_info = match_info;

    omx__debug_assert(rreq->generic.state & OMX_REQUEST_STATE_RECV_NEED_MATCHING);
    rreq->generic.state &= ~OMX_REQUEST_STATE_RECV_NEED_MATCHING;

    rreq->generic.status.msg_length = msg_length;
    xfer_length = rreq->recv.length < msg_length ? rreq->recv.length : msg_length;
    rreq->generic.status.xfer_length = xfer_length;
    sreq->generic.status.xfer_length = xfer_length;

    memcpy(rreq->recv.buffer, sbuffer, xfer_length);

    sreq->generic.state = 0; /* the state of expected self send is always set here */
    omx__send_complete(ep, sreq, OMX_STATUS_SUCCESS);
    omx__recv_complete(ep, rreq, OMX_STATUS_SUCCESS);

  } else {
    /* unexpected, even after the handler */
    void *unexp_buffer = NULL;

    rreq = omx__request_alloc(ep);
    if (unlikely(!rreq))
      return OMX_NO_RESOURCES;

    rreq->generic.type = OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED;

    if (msg_length) {
      unexp_buffer = malloc(msg_length);
      if (unlikely(!unexp_buffer)) {
	fprintf(stderr, "Failed to allocate buffer for unexpected message, dropping\n");
	omx__request_free(ep, rreq);
	return OMX_NO_RESOURCES;
      }
    }
    rreq->recv.buffer = unexp_buffer;

    rreq->generic.partner = ep->myself;
    omx__partner_to_addr(ep->myself, &rreq->generic.status.addr);
    rreq->generic.status.match_info = match_info;
    rreq->generic.status.msg_length = msg_length;

    rreq->recv.specific.self_unexp.sreq = sreq;
    memcpy(unexp_buffer, sbuffer, msg_length);

    rreq->generic.state = OMX_REQUEST_STATE_RECV_UNEXPECTED; /* the state of unexpected self send is always set here */
    omx__enqueue_request(&ep->ctxid[ctxid].unexp_req_q, rreq);

    /* self communication are always synchronous,
     * the send will be completed on matching
     */
    sreq->generic.state = OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED; /* the state of unexpected self recv is always set here */
    omx__enqueue_request(&ep->send_self_unexp_req_q, sreq);
  }

  return OMX_SUCCESS;
}

/***********************
 * Truc Message Receive
 */

omx_return_t
omx__process_recv_truc(struct omx_endpoint *ep,
		       struct omx_evt_recv_truc *truc)
{
  union omx__truc_data *data_n = (void *) truc->data;
  uint8_t truc_type = OMX_FROM_PKT_FIELD(data_n->type);
  struct omx__partner *partner;
  omx_return_t ret;

  ret = omx__partner_recv_lookup(ep, truc->peer_index, truc->src_endpoint,
				   &partner);
  if (unlikely(ret != OMX_SUCCESS))
    return ret;

  switch (truc_type) {
  case OMX__TRUC_DATA_TYPE_ACK: {
    omx__handle_truc_ack(ep, partner, &data_n->ack);
    break;
  }
  default:
    omx__abort("Failed to handle truc message with type %d\n", truc_type);
  }

  return OMX_SUCCESS;
}

/**************************
 * Main recv routines
 */

omx_return_t
omx_irecv(struct omx_endpoint *ep,
	  void *buffer, size_t recv_length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, union omx_request **requestp)
{
  union omx_request * req;
  omx_return_t ret;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  uint32_t msg_length;
  uint32_t xfer_length;

  if (unlikely(match_info & ~match_mask)) {
    ret = OMX_BAD_MATCH_MASK;
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(!CHECK_MATCHING_WITH_CTXID(ep, match_mask))) {
    ret = OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
    goto out;
  }

  omx__foreach_request(&ep->ctxid[ctxid].unexp_req_q, req) {
    if (likely((req->generic.status.match_info & match_mask) == match_info)) {
      /* matched an unexpected */
      omx__dequeue_request(&ep->ctxid[ctxid].unexp_req_q, req);

      /* compute xfer_length */
      msg_length = req->generic.status.msg_length;
      xfer_length = recv_length < msg_length ? recv_length : msg_length;
      req->generic.status.xfer_length = xfer_length;

      omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED);
      req->generic.state &= ~OMX_REQUEST_STATE_RECV_UNEXPECTED;

      req->generic.status.context = context;

      if (unlikely(req->generic.type == OMX_REQUEST_TYPE_RECV_LARGE)) {
	/* it's a large message, queue the recv large */
	req->recv.buffer = buffer;
	omx__submit_or_queue_pull(ep, req);

      } else if (unlikely(req->generic.type == OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED)) {
	/* it's a unexpected from self, we need to complete the corresponding send */
	union omx_request *sreq = req->recv.specific.self_unexp.sreq;

	memcpy(buffer, req->recv.buffer, xfer_length);
	if (msg_length)
	  free(req->recv.buffer);
	req->recv.buffer = buffer;
	omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);

	omx__debug_assert(sreq->generic.state & OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED);
	sreq->generic.state &= ~OMX_REQUEST_STATE_SEND_SELF_UNEXPECTED;
	omx__dequeue_request(&ep->send_self_unexp_req_q, sreq);
	sreq->generic.status.xfer_length = xfer_length;
	omx__send_complete(ep, sreq, OMX_STATUS_SUCCESS);

      } else {
	/* it's a tiny/small/medium, copy the data back to our buffer */
	memcpy(buffer, req->recv.buffer, xfer_length); /* FIXME: could just copy what has been received */
	if (msg_length)
	  free(req->recv.buffer);
	req->recv.buffer = buffer;

	if (unlikely(req->generic.state)) {
	  omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL);
	  omx__enqueue_request(&ep->multifrag_medium_recv_req_q, req);
	} else {
	  omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);
	}
      }

      goto ok;
    }
  }

  /* allocate a new recv request */
  req = omx__request_alloc(ep);
  if (unlikely(!req)) {
    ret = OMX_NO_RESOURCES;
    goto out;
  }

  req->generic.type = OMX_REQUEST_TYPE_RECV;
  req->generic.state = OMX_REQUEST_STATE_RECV_NEED_MATCHING; /* the state of non-matched recv is always set here */
  req->generic.status.context = context;
  req->recv.buffer = buffer;
  req->recv.length = recv_length;
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
