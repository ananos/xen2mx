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

#include <sys/ioctl.h>

#include "omx_lib.h"
#include "omx_lib_wire.h"
#include "omx_wire_access.h"
#include "omx_request.h"

/*************************************
 * Apply a ack or a nack to a request
 */

static void
omx__mark_request_acked(struct omx_endpoint *ep,
			union omx_request *req,
			omx_status_code_t status)
{
  struct list_head *queue;

  if (req->generic.state & OMX_REQUEST_STATE_REQUEUED)
    queue = &ep->requeued_send_req_q;
  else
    queue = &ep->non_acked_req_q;

  omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_ACK);
  req->generic.state &= ~(OMX_REQUEST_STATE_NEED_ACK|OMX_REQUEST_STATE_REQUEUED);

  switch (req->generic.type) {

  case OMX_REQUEST_TYPE_SEND_TINY:
  case OMX_REQUEST_TYPE_SEND_SMALL:
    omx__dequeue_request(queue, req);
    omx__send_complete(ep, req, status);
    break;

  case OMX_REQUEST_TYPE_SEND_MEDIUM:
    if (unlikely(req->generic.state & OMX_REQUEST_STATE_IN_DRIVER)) {
      /* keep the request in the driver_posted_req_q for now until it returns from the driver */
      if (req->generic.status.code == OMX_STATUS_SUCCESS)
	/* set the status (success for ack, error for nack) only if there has been no error early */
	req->generic.status.code = status;
    } else {
      omx__dequeue_request(queue, req);
      omx__send_complete(ep, req, status);
    }
    break;

  case OMX_REQUEST_TYPE_SEND_LARGE:
    /* if the request was already replied, it would have been acked at the same time */
    omx__dequeue_request(queue, req);
    if (unlikely(status != OMX_STATUS_SUCCESS)) {
      /* the request has been nacked, there won't be any reply */
      req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;
      omx__send_complete(ep, req, status);
    } else {
      if (req->generic.state & OMX_REQUEST_STATE_NEED_REPLY)
	omx__enqueue_request(&ep->large_send_req_q, req);
      else
	omx__send_complete(ep, req, status);
    }
    break;

  case OMX_REQUEST_TYPE_RECV_LARGE:
    omx__dequeue_request(queue, req);
    omx__recv_complete(ep, req, status);
    break;

  default:
    omx__abort("Failed to to ack unexpected request type %d\n",
	       req->generic.type);
  }
}

/***********************
 * Handle Received Acks
 */

omx_return_t
omx__handle_ack(struct omx_endpoint *ep,
		struct omx__partner *partner, omx__seqnum_t ack_before)
{
  /* take care of the seqnum wrap around by casting differences into omx__seqnum_t */
  omx__seqnum_t missing_acks = OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq);
  omx__seqnum_t new_acks = OMX__SEQNUM(ack_before - partner->next_acked_send_seq);

  if (!new_acks || new_acks > missing_acks) {
    omx__debug_printf(ACK, "obsolete ack up to %d, %d new for %d missing\n",
		      (unsigned) OMX__SEQNUM(ack_before - 1), (unsigned) new_acks, (unsigned) missing_acks);

  } else {
    union omx_request *req, *next;

    omx__debug_printf(ACK, "ack up to %d\n", (unsigned) OMX__SEQNUM(ack_before - 1));

    omx__foreach_partner_non_acked_request_safe(partner, req, next) {
      /* take care of the seqnum wrap around here too */
      omx__seqnum_t req_index = OMX__SEQNUM(req->generic.send_seqnum - partner->next_acked_send_seq);

      /* ack req_index from 0 to new_acks-1 */
      if (req_index >= new_acks)
	break;

      omx__dequeue_partner_non_acked_request(partner, req);
      omx__mark_request_acked(ep, req, OMX_STATUS_SUCCESS);
    }

    partner->next_acked_send_seq = ack_before;
  }

  return OMX_SUCCESS;
}

void
omx__handle_truc_ack(struct omx_endpoint *ep,
		     struct omx__partner *partner,
		     struct omx__truc_ack_data *ack_n)
{
  omx__seqnum_t ack = OMX_FROM_PKT_FIELD(ack_n->lib_seqnum);
  uint32_t acknum = OMX_FROM_PKT_FIELD(ack_n->acknum);

  if (acknum <= partner->last_recv_acknum) {
    omx__debug_printf(ACK, "got obsolete acknum %d, expected more than %d\n",
		      (unsigned) acknum, (unsigned) partner->last_recv_acknum);
    return;
  }
  partner->last_recv_acknum = acknum;

  omx__debug_printf(ACK, "got a ack up to %d\n", (unsigned) ack);
  omx__handle_ack(ep, partner, ack);
}

/************************
 * Handle Received Nacks
 */

omx_return_t
omx__handle_nack(struct omx_endpoint *ep,
		 struct omx__partner *partner, omx__seqnum_t seqnum,
		 omx_status_code_t status)
{
  omx__seqnum_t nack_index = OMX__SEQNUM(seqnum - partner->next_acked_send_seq);
  union omx_request *req;

  /* look in the list of pending real messages */
  omx__foreach_partner_non_acked_request(partner, req) {
    omx__seqnum_t req_index = OMX__SEQNUM(req->generic.send_seqnum - partner->next_acked_send_seq);

    if (nack_index < req_index)
      break;

    if (nack_index == req_index) {
      omx__dequeue_partner_non_acked_request(partner, req);
      omx__mark_request_acked(ep, req, status);
      return OMX_SUCCESS;
    }
  }

  /* look in the list of pending connect requests */
  omx__foreach_partner_connect_request(partner, req) {
    /* FIXME: if > then break,
     * but take care of the wrap around using partner->connect_seqnum
     * but this protocol is crap so far since we can't distinguish between nacks for send and connect
     */
    if (req->connect.connect_seqnum == seqnum) {
      omx__connect_complete(ep, req, status);
      return OMX_SUCCESS;
    }
  }

  omx__debug_printf(ACK, "Failed to find request to nack for seqnum %d, could be a duplicate, ignoring\n",
		    seqnum);
  return OMX_SUCCESS;
}

/**********************
 * Handle Acks to Send
 */

static omx_return_t
omx__submit_send_liback(struct omx_endpoint *ep,
			struct omx__partner * partner)
{
  struct omx_cmd_send_truc truc_param;
  union omx__truc_data *data_n = (void *) &truc_param.data;
  int err;

  partner->last_send_acknum++;

  truc_param.hdr.peer_index = partner->peer_index;
  truc_param.hdr.dest_endpoint = partner->endpoint_index;
  truc_param.hdr.shared = omx__partner_localization_shared(partner);
  truc_param.hdr.length = sizeof(union omx__truc_data);
  truc_param.hdr.session_id = partner->back_session_id;
  OMX_PKT_FIELD_FROM(data_n->type, OMX__TRUC_DATA_TYPE_ACK);
  OMX_PKT_FIELD_FROM(data_n->ack.acknum, partner->last_send_acknum);
  OMX_PKT_FIELD_FROM(data_n->ack.session_id, partner->back_session_id);
  OMX_PKT_FIELD_FROM(data_n->ack.lib_seqnum, partner->next_frag_recv_seq);
  OMX_PKT_FIELD_FROM(data_n->ack.send_seq, partner->next_frag_recv_seq); /* FIXME? partner->send_seq */
  OMX_PKT_FIELD_FROM(data_n->ack.requeued, 0); /* FIXME? partner->requeued */

  err = ioctl(ep->fd, OMX_CMD_SEND_TRUC, &truc_param);
  if (unlikely(err < 0))
    return omx__errno_to_return("ioctl SEND_TRUC");

  /* no need to wait for a done event, tiny is synchronous */

  return OMX_SUCCESS;
}

omx_return_t
omx__process_partners_to_ack(struct omx_endpoint *ep)
{
  struct omx__partner *partner, *next;
  uint64_t now = omx__driver_desc->jiffies;
  omx_return_t ret = OMX_SUCCESS;

  /* no need to bother looking in the queue if the time didn't change */
  static uint64_t last_invokation = 0;
  if (now == last_invokation)
    return OMX_SUCCESS;
  last_invokation = now;

  list_for_each_entry_safe(partner, next,
			   &ep->partners_to_ack, endpoint_partners_to_ack_elt) {
    if (now - partner->oldest_recv_time_not_acked < omx__globals.ack_delay_jiffies)
      /* the remaining ones are more recent, no need to ack them yet */
      break;

    omx__debug_printf(ACK, "acking back partner (%lld>>%lld)\n",
		      (unsigned long long) now,
		      (unsigned long long) partner->oldest_recv_time_not_acked);

    ret = omx__submit_send_liback(ep, partner);
    if (ret != OMX_SUCCESS)
      /* failed to send one liback, no need to try more */
      break;

    omx__partner_ack_sent(ep, partner);
  }

  return ret;
}

omx_return_t
omx__flush_partners_to_ack(struct omx_endpoint *ep)
{
  struct omx__partner *partner, *next;
  omx_return_t ret = OMX_SUCCESS;

  list_for_each_entry_safe(partner, next,
			   &ep->partners_to_ack, endpoint_partners_to_ack_elt) {
    omx__debug_printf(ACK, "forcing ack back partner (%lld>>%lld)\n",
		      (unsigned long long) omx__driver_desc->jiffies,
		      (unsigned long long) partner->oldest_recv_time_not_acked);

    ret = omx__submit_send_liback(ep, partner);
    if (ret != OMX_SUCCESS)
      /* failed to send one liback, too bad for this peer */
      continue;

    omx__partner_ack_sent(ep, partner);
  }

  return ret;
}

void
omx__prepare_progress_wakeup(struct omx_endpoint *ep)
{
  union omx_request *req;
  struct omx__partner *partner;
  uint64_t now = omx__driver_desc->jiffies;
  uint64_t wakeup_jiffies = OMX_NO_WAKEUP_JIFFIES;

  /* any delayed ack to send soon? */
  if (!list_empty(&ep->partners_to_ack)) {
    uint64_t tmp;

    partner = list_first_entry(&ep->partners_to_ack, struct omx__partner, endpoint_partners_to_ack_elt);
    tmp = partner->oldest_recv_time_not_acked + omx__globals.ack_delay_jiffies;

    omx__debug_printf(WAIT, "need to wakeup at %lld jiffies (in %ld) for delayed acks\n",
		      (unsigned long long) tmp, (unsigned long) (tmp-now));

    if (tmp < wakeup_jiffies || wakeup_jiffies == OMX_NO_WAKEUP_JIFFIES)
      wakeup_jiffies = tmp;
  }

  /* any send to resend soon? */
  if (!omx__queue_empty(&ep->non_acked_req_q)) {
    uint64_t tmp;

    req = omx__queue_first_request(&ep->non_acked_req_q);
    tmp = req->generic.last_send_jiffies + omx__globals.resend_delay_jiffies;

    omx__debug_printf(WAIT, "need to wakeup at %lld jiffies (in %ld) for resend\n",
		      (unsigned long long) tmp, (unsigned long) (tmp-now));

    if (tmp < wakeup_jiffies || wakeup_jiffies == OMX_NO_WAKEUP_JIFFIES)
      wakeup_jiffies = tmp;
  }

  /* any connect to resend soon? */
  if (!omx__queue_empty(&ep->connect_req_q)) {
    uint64_t tmp;

    req = omx__queue_first_request(&ep->connect_req_q);
    tmp = req->generic.last_send_jiffies + omx__globals.resend_delay_jiffies;

    omx__debug_printf(WAIT, "need to wakeup at %lld jiffies (in %ld) for resend\n",
		      (unsigned long long) tmp, (unsigned long) (tmp-now));

    if (tmp < wakeup_jiffies || wakeup_jiffies == OMX_NO_WAKEUP_JIFFIES)
      wakeup_jiffies = tmp;
  }

  ep->desc->wakeup_jiffies = wakeup_jiffies;
}

/**********************************
 * Set Request or Endpoint Timeout
 */

omx_return_t
omx_set_request_timeout(struct omx_endpoint *ep,
			union omx_request *request, uint32_t ms)
{
  uint32_t delay_jiffies = omx__timeout_ms_to_relative_jiffies(ms);

  if (request)
    request->generic.resend_delay_jiffies = delay_jiffies;
  else
    ep->resend_delay_jiffies = delay_jiffies;

  return OMX_SUCCESS;
}

