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

/*******
 * Acks
 */

static void
omx__mark_request_acked(struct omx_endpoint *ep,
			union omx_request *req)
{
  omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_ACK);
  req->generic.state &= ~OMX_REQUEST_STATE_NEED_ACK;

  switch (req->generic.type) {

  case OMX_REQUEST_TYPE_SEND_TINY:
  case OMX_REQUEST_TYPE_SEND_SMALL:
    omx__dequeue_request(&ep->non_acked_req_q, req);
    omx__send_complete(ep, req, OMX_STATUS_SUCCESS);
    break;

  case OMX_REQUEST_TYPE_SEND_MEDIUM:
    if (unlikely(req->generic.state & OMX_REQUEST_STATE_IN_DRIVER)) {
      /* keep in the sent_req_q for now */
    } else {
      omx__dequeue_request(&ep->non_acked_req_q, req);
      omx__send_complete(ep, req, OMX_STATUS_SUCCESS);
    }
    break;

  case OMX_REQUEST_TYPE_SEND_LARGE:
    /* if the request was already replied, it would have been acked at the same time */
    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_REPLY);

    omx__dequeue_request(&ep->non_acked_req_q, req);
    omx__enqueue_request(&ep->large_send_req_q, req);
    break;

  case OMX_REQUEST_TYPE_RECV_LARGE:
    omx__dequeue_request(&ep->non_acked_req_q, req);
    omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);
    break;

  default:
    omx__abort("Failed to to ack unexpected request type %d\n",
	       req->generic.type);
  }
}

omx_return_t
omx__handle_ack(struct omx_endpoint *ep,
		struct omx__partner *partner, omx__seqnum_t last_to_ack)
{
  /* take care of the seqnum wrap around by casting differences into omx__seqnum_t */
  omx__seqnum_t missing_acks = partner->next_send_seq - 1 - partner->last_acked_send_seq;
  omx__seqnum_t new_acks = last_to_ack - partner->last_acked_send_seq;

  if (!new_acks || new_acks > missing_acks) {
    omx__debug_printf("obsolete ack up to %d\n", (unsigned) last_to_ack);
  } else {
    union omx_request *req, *next;

    omx__debug_printf("ack up to %d\n", (unsigned) last_to_ack);

    omx__foreach_partner_non_acked_request_safe(partner, req, next) {
      if (req->generic.send_seqnum > last_to_ack)
	break;

      omx__dequeue_partner_non_acked_request(partner, req);
      omx__mark_request_acked(ep, req);
    }

    partner->last_acked_send_seq = last_to_ack;
  }

  return OMX_SUCCESS;
}

static omx_return_t
omx__submit_send_liback(struct omx_endpoint *ep,
			struct omx__partner * partner)
{
  struct omx_cmd_send_truc truc_param;
  union omx__truc_data *data_n = (void *) &truc_param.data;
  int err;

  truc_param.peer_index = partner->peer_index;
  truc_param.dest_endpoint = partner->endpoint_index;
  truc_param.length = sizeof(union omx__truc_data);
  truc_param.session_id = partner->session_id;
  OMX_PKT_FIELD_FROM(data_n->type, OMX__TRUC_DATA_TYPE_ACK);
  // OMX_PKT_FIELD_FROM(data_n->ack.acknum, partner->send_acknum++);
  OMX_PKT_FIELD_FROM(data_n->ack.session_id, partner->session_id);
  OMX_PKT_FIELD_FROM(data_n->ack.lib_seqnum, partner->next_frag_recv_seq - 1);
  // OMX_PKT_FIELD_FROM(data_n->ack.send_seq, partner->send_seq);
  // OMX_PKT_FIELD_FROM(data_n->ack.requeue, partner->requeued);

  err = ioctl(ep->fd, OMX_CMD_SEND_TRUC, &truc_param);
  if (unlikely(err < 0))
    return omx__errno_to_return("ioctl SEND_TINY");

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
    if (now - partner->oldest_recv_time_not_acked < omx__globals.ack_delay)
      /* the remaining ones are more recent, no need to ack them yet */
      break;

    omx__debug_printf("acking back partner (%lld>>%lld)\n",
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
    omx__debug_printf("forcing ack back partner (%lld>>%lld)\n",
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
