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

#include <sys/ioctl.h>

#include "omx_lib.h"
#include "omx_segments.h"
#include "omx_request.h"
#include "omx_lib_wire.h"
#include "omx_wire_access.h"

/**************************
 * Send Request Completion
 */

void
omx__send_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_return_t status)
{
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);

  if (likely(req->generic.status.code == OMX_SUCCESS)) {
    /* only set the status if it is not already set to an error */
    if (likely(status == OMX_SUCCESS)) {
      if (unlikely(req->generic.status.xfer_length < req->generic.status.msg_length))
	req->generic.status.code = omx__error_with_req(ep, req, OMX_MESSAGE_TRUNCATED,
						       "Completing send request, truncated from %ld to %ld bytes",
						       req->generic.status.msg_length, req->generic.status.xfer_length);
    } else {
      req->generic.status.code = omx__error_with_req(ep, req, status, "Completing send request");
    }
  }

  if (req->generic.state & OMX_REQUEST_STATE_SEND_NEED_SEQNUM)
    goto nothing_specific;

  switch (req->generic.type) {
  case OMX_REQUEST_TYPE_SEND_SMALL:
    free(req->send.specific.small.copy);
    break;
  case OMX_REQUEST_TYPE_SEND_MEDIUM:
    omx__endpoint_sendq_map_put(ep, req->send.specific.medium.frags_nr, req->send.specific.medium.sendq_map_index);
    break;
  default:
    break;
  }

 nothing_specific:

  /* the request is acked, we can free the segments */
  omx_free_segments(&req->send.segs);

  omx__notify_request_done(ep, ctxid, req);
}

/************
 * Send Tiny
 */

static INLINE void
omx__post_isend_tiny(struct omx_endpoint *ep,
		     struct omx__partner *partner,
		     union omx_request * req)
{
  struct omx_cmd_send_tiny * tiny_param = &req->send.specific.tiny.send_tiny_ioctl_param;
  omx__seqnum_t ack_upto = omx__get_partner_needed_ack(ep, partner);
  int err;

  omx__debug_printf(ACK, "piggy acking back to partner up to %d (#%d) at jiffies %lld\n",
		    (unsigned int) OMX__SEQNUM(ack_upto - 1),
		    (unsigned int) OMX__SESNUM_SHIFTED(ack_upto - 1),
		    (unsigned long long) omx__driver_desc->jiffies);
  tiny_param->hdr.piggyack = ack_upto;

  err = ioctl(ep->fd, OMX_CMD_SEND_TINY, tiny_param);
  if (unlikely(err < 0)) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "send tiny message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE void
omx__submit_or_queue_isend_tiny(struct omx_endpoint *ep,
				union omx_request * req,
				struct omx__partner * partner)
{
  struct omx_cmd_send_tiny * tiny_param;
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  uint32_t length = req->send.segs.total_length;
  omx__seqnum_t seqnum;

  req->generic.type = OMX_REQUEST_TYPE_SEND_TINY;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.resends = 0;

  tiny_param = &req->send.specific.tiny.send_tiny_ioctl_param;
  tiny_param->hdr.peer_index = partner->peer_index;
  tiny_param->hdr.dest_endpoint = partner->endpoint_index;
  tiny_param->hdr.shared = omx__partner_localization_shared(partner);
  tiny_param->hdr.match_info = match_info;
  tiny_param->hdr.length = length;
  tiny_param->hdr.seqnum = seqnum;
  tiny_param->hdr.session_id = partner->true_session_id;
  omx_copy_from_segments(tiny_param->data, &req->send.segs, length);

  omx__post_isend_tiny(ep, partner, req);

  /* no need to wait for a done event, tiny is synchronous */

  req->generic.send_seqnum = seqnum;
  req->generic.resends_max = ep->req_resends_max;
  req->generic.state = OMX_REQUEST_STATE_NEED_ACK; /* the state of send tiny is always initialized here */
  omx__enqueue_partner_non_acked_request(partner, req);

  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);
}

/*************
 * Send Small
 */

static INLINE void
omx__post_isend_small(struct omx_endpoint *ep,
		      struct omx__partner *partner,
		      union omx_request * req)
{
  struct omx_cmd_send_small * small_param = &req->send.specific.small.send_small_ioctl_param;
  omx__seqnum_t ack_upto = omx__get_partner_needed_ack(ep, partner);
  int err;

  omx__debug_printf(ACK, "piggy acking back to partner up to %d (#%d) at jiffies %lld\n",
		    (unsigned int) OMX__SEQNUM(ack_upto - 1),
		    (unsigned int) OMX__SESNUM_SHIFTED(ack_upto - 1),
		    (unsigned long long) omx__driver_desc->jiffies);
  small_param->piggyack = ack_upto;

  err = ioctl(ep->fd, OMX_CMD_SEND_SMALL, small_param);
  if (unlikely(err < 0)) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "send small message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE omx_return_t
omx__submit_isend_small(struct omx_endpoint *ep,
			union omx_request *req)
{
  struct omx_cmd_send_small * small_param;
  struct omx__partner * partner = req->generic.partner;
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  uint32_t length = req->generic.status.msg_length;
  void *copy;

  copy = malloc(length);
  if (unlikely(!copy))
    return OMX_INTERNAL_NEED_RETRY;

  small_param = &req->send.specific.small.send_small_ioctl_param;
  small_param->peer_index = partner->peer_index;
  small_param->dest_endpoint = partner->endpoint_index;
  small_param->shared = omx__partner_localization_shared(partner);
  small_param->match_info = match_info;
  small_param->length = length;
  small_param->seqnum = req->generic.send_seqnum;
  small_param->session_id = partner->true_session_id;

  /*
   * if single segment, use it for the first pio,
   * else copy it in the contigous copy buffer first
   */
  if (likely(req->send.segs.nseg == 1)) {
    small_param->vaddr = (uintptr_t) req->send.segs.single.ptr;
  } else {
    omx_copy_from_segments(copy, &req->send.segs, length);
    small_param->vaddr = (uintptr_t) copy;
  }

  omx__post_isend_small(ep, partner, req);

  /* no need to wait for a done event, small is synchronous */

  /* bufferize data for retransmission (if not done already) */
  if (likely(req->send.segs.nseg == 1)) {
    omx_copy_from_segments(copy, &req->send.segs, length);
    small_param->vaddr = (uintptr_t) copy;
  }
  req->send.specific.small.copy = copy;

  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;
  req->generic.state = OMX_REQUEST_STATE_NEED_ACK; /* the state of send small is always initialized here */
  omx__enqueue_partner_non_acked_request(partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);

  return OMX_SUCCESS;
}

static INLINE void
omx__submit_or_queue_isend_small(struct omx_endpoint *ep,
				 union omx_request *req,
				 struct omx__partner * partner)
{
  uint32_t length = req->send.segs.total_length;
  omx__seqnum_t seqnum;
  omx_return_t ret;

  req->generic.type = OMX_REQUEST_TYPE_SEND_SMALL;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;

  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  ret = omx__submit_isend_small(ep, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_NEED_RETRY);
    omx__debug_printf(SEND, "queueing send request %p\n", req);
    req->generic.state = OMX_REQUEST_STATE_QUEUED; /* the state of send small is initialized here (or in submit() above) */
    omx__enqueue_request(&ep->queued_send_req_q, req);
  }
}

/**************
 * Send Medium
 */

static void
omx__post_isend_medium(struct omx_endpoint *ep,
		       struct omx__partner *partner,
		       union omx_request *req)
{
  struct omx_cmd_send_medium * medium_param = &req->send.specific.medium.send_medium_ioctl_param;
  omx__seqnum_t ack_upto = omx__get_partner_needed_ack(ep, partner);
  uint32_t length = req->generic.status.msg_length;
  uint32_t remaining = length;
  int * sendq_index = req->send.specific.medium.sendq_map_index;
  int frags_nr = req->send.specific.medium.frags_nr;
  int err;
  int i;

  omx__debug_printf(ACK, "piggy acking back to partner up to %d (#%d) at jiffies %lld\n",
		    (unsigned int) OMX__SEQNUM(ack_upto - 1),
		    (unsigned int) OMX__SESNUM_SHIFTED(ack_upto - 1),
		    (unsigned long long) omx__driver_desc->jiffies);
  medium_param->piggyack = ack_upto;

  if (likely(req->send.segs.nseg == 1)) {
    /* optimize the contigous send medium */
    void * data = req->send.segs.single.ptr;
    uint32_t offset = 0;

    for(i=0; i<frags_nr; i++) {
      unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
        ? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
      medium_param->frag_length = chunk;
      medium_param->frag_seqnum = i;
      medium_param->sendq_page_offset = sendq_index[i];
      omx__debug_printf(MEDIUM, "sending medium seqnum %d pipeline 2 length %d of total %ld\n",
			i, chunk, (unsigned long) length);

      /* copy the data in the sendq only once */
      if (likely(!(req->generic.state & OMX_REQUEST_STATE_REQUEUED)))
	memcpy(ep->sendq + (sendq_index[i] << OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT), data + offset, chunk);

      err = ioctl(ep->fd, OMX_CMD_SEND_MEDIUM, medium_param);
      if (unlikely(err < 0)) {
	/* finish copying frags if not done already */
	if (likely(!(req->generic.state & OMX_REQUEST_STATE_REQUEUED))) {
	  int j;
	  for(j=i+1; j<frags_nr; i++) {
	    unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
	      ? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
	    memcpy(ep->sendq + (sendq_index[j] << OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT), data + offset, chunk);
	    remaining -= chunk;
	    offset += chunk;
	  }
	}
	goto err;
      }

      ep->avail_exp_events--;
      remaining -= chunk;
      offset += chunk;
    }

  } else {
    /* initialize the state to the beginning */
    struct omx_segscan_state state = { .seg = &req->send.segs.segs[0], .offset = 0 };

    for(i=0; i<frags_nr; i++) {
      unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
        ? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
      medium_param->frag_length = chunk;
      medium_param->frag_seqnum = i;
      medium_param->sendq_page_offset = sendq_index[i];
      omx__debug_printf(MEDIUM, "sending medium seqnum %d pipeline 2 length %d of total %ld\n",
			i, chunk, (unsigned long) length);

      /* copy the data in the sendq only once */
      if (likely(!(req->generic.state & OMX_REQUEST_STATE_REQUEUED)))
	omx_continue_partial_copy_from_segments(ep->sendq + (sendq_index[i] << OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT),
						&req->send.segs, chunk,
						&state);

      err = ioctl(ep->fd, OMX_CMD_SEND_MEDIUM, medium_param);
      if (unlikely(err < 0)) {
	/* finish copying frags if not done already */
	if (likely(!(req->generic.state & OMX_REQUEST_STATE_REQUEUED))) {
	  int j;
	  for(j=i+1; j<frags_nr; i++) {
	    unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
	      ? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
	    omx_continue_partial_copy_from_segments(ep->sendq + (sendq_index[j] << OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT),
						    &req->send.segs, chunk,
						    &state);
	    remaining -= chunk;
	  }
	}
	goto err;
      }

      ep->avail_exp_events--;
      remaining -= chunk;
    }
  }

  req->send.specific.medium.frags_pending_nr = frags_nr;

 ok:
  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  req->generic.state |= OMX_REQUEST_STATE_IN_DRIVER;
  omx__enqueue_request(&ep->driver_posted_req_q, req);

  /* at least one frag was posted, the ack has been sent for sure */
  omx__mark_partner_ack_sent(ep, partner);

  return;

 err:
  /* assume some frags got lost and let retransmission take care of it later */
  omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				     OMX_SUCCESS,
				     "send medium message fragment");

  req->send.specific.medium.frags_pending_nr = i;
  if (i)
    /*
     * some frags were posted, mark the request as IN_DRIVER
     * and let retransmission wait for send done events first
     */
    goto ok;

  /*
   * no frags were posted, keep the request as NEED_ACK
   * and let retransmission occur later
   */
  omx__enqueue_request(&ep->non_acked_req_q, req);
}

static INLINE omx_return_t
omx__submit_isend_medium(struct omx_endpoint *ep,
			 union omx_request *req)
{
  struct omx_cmd_send_medium * medium_param = &req->send.specific.medium.send_medium_ioctl_param;
  struct omx__partner * partner = req->generic.partner;
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  uint32_t length = req->generic.status.msg_length;
  int * sendq_index = req->send.specific.medium.sendq_map_index;
  int frags_nr;

  frags_nr = OMX_MEDIUM_FRAGS_NR(length);
  omx__debug_assert(frags_nr <= 8); /* for the sendq_index array above */
  req->send.specific.medium.frags_nr = frags_nr;

  if (unlikely(ep->avail_exp_events < frags_nr
	       || omx__endpoint_sendq_map_get(ep, frags_nr, req, sendq_index) < 0))
    /* let the caller handle the error */
    return OMX_INTERNAL_NEED_RETRY;

  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;
  req->generic.state = OMX_REQUEST_STATE_NEED_ACK; /* the state of send medium is initialized here and modified in post() (or set to QUEUED in submit_or_queue()) */
  omx__enqueue_partner_non_acked_request(partner, req);

  medium_param->peer_index = partner->peer_index;
  medium_param->dest_endpoint = partner->endpoint_index;
  medium_param->shared = omx__partner_localization_shared(partner);
  medium_param->match_info = req->generic.status.match_info;
  medium_param->frag_pipeline = OMX_MEDIUM_FRAG_PIPELINE;
  medium_param->msg_length = length;
  medium_param->seqnum = req->generic.send_seqnum;
  medium_param->session_id = partner->true_session_id;

  omx__post_isend_medium(ep, partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);

  return OMX_SUCCESS;
}

static INLINE void
omx__submit_or_queue_isend_medium(struct omx_endpoint *ep,
				  union omx_request *req,
				  struct omx__partner * partner)
{
  uint32_t length = req->send.segs.total_length;
  omx__seqnum_t seqnum;
  omx_return_t ret;

  req->generic.type = OMX_REQUEST_TYPE_SEND_MEDIUM;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;

  /* need to wait for a done event, since the sendq pages
   * might still be in use
   */
  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  ret = omx__submit_isend_medium(ep, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_NEED_RETRY);
    omx__debug_printf(SEND, "queueing medium request %p\n", req);
    req->generic.state = OMX_REQUEST_STATE_QUEUED; /* the state of send medium is initialized here (or in submit() above) */
    omx__enqueue_request(&ep->queued_send_req_q, req);
  }
}

/************
 * Send Rndv
 */

static INLINE void
omx__post_isend_rndv(struct omx_endpoint *ep,
		     struct omx__partner *partner,
		     union omx_request * req)
{
  struct omx_cmd_send_rndv * rndv_param = &req->send.specific.large.send_rndv_ioctl_param;
  omx__seqnum_t ack_upto = omx__get_partner_needed_ack(ep, partner);
  int err;

  omx__debug_printf(ACK, "piggy acking back to partner up to %d (#%d) at jiffies %lld\n",
		    (unsigned int) OMX__SEQNUM(ack_upto - 1),
		    (unsigned int) OMX__SESNUM_SHIFTED(ack_upto - 1),
		    (unsigned long long) omx__driver_desc->jiffies);
  rndv_param->hdr.piggyack = ack_upto;

  err = ioctl(ep->fd, OMX_CMD_SEND_RNDV, rndv_param);
  if (unlikely(err < 0)) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "send rndv message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE omx_return_t
omx__submit_isend_rndv(struct omx_endpoint *ep,
		       union omx_request *req)
{
  struct omx_cmd_send_rndv * rndv_param = &req->send.specific.large.send_rndv_ioctl_param;
  struct omx__rndv_data * data_n = (void *) rndv_param->data;
  struct omx__large_region *region;
  struct omx__partner * partner = req->generic.partner;
  uint64_t match_info = req->generic.status.match_info;
  uint32_t length = req->generic.status.msg_length;
  omx_return_t ret;

  ret = omx__get_region(ep, &req->send.segs, &region, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_NEED_RETRY);
    /* let the caller handle the error */
    return ret;
  }

  req->generic.state = OMX_REQUEST_STATE_NEED_REPLY|OMX_REQUEST_STATE_NEED_ACK; /* the state of send medium is always initialized here */
  omx__enqueue_partner_non_acked_request(partner, req);

  rndv_param->hdr.peer_index = partner->peer_index;
  rndv_param->hdr.dest_endpoint = partner->endpoint_index;
  rndv_param->hdr.shared = omx__partner_localization_shared(partner);
  rndv_param->hdr.match_info = match_info;
  rndv_param->hdr.length = sizeof(struct omx__rndv_data);
  rndv_param->hdr.seqnum = req->generic.send_seqnum;
  rndv_param->hdr.session_id = partner->true_session_id;

  OMX_PKT_FIELD_FROM(data_n->msg_length, length);
  OMX_PKT_FIELD_FROM(data_n->rdma_id, region->id);
  OMX_PKT_FIELD_FROM(data_n->rdma_seqnum, region->seqnum);
  OMX_PKT_FIELD_FROM(data_n->rdma_offset, region->offset);

  omx__post_isend_rndv(ep, partner, req);

  /* no need to wait for a done event, tiny is synchronous */

  req->send.specific.large.region = region;

  return OMX_SUCCESS;
}

static INLINE void
omx__submit_or_queue_isend_large(struct omx_endpoint *ep,
				 union omx_request *req,
				 struct omx__partner * partner)
{
  uint32_t length = req->send.segs.total_length;
  omx__seqnum_t seqnum;
  omx_return_t ret;

  req->generic.type = OMX_REQUEST_TYPE_SEND_LARGE;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  req->generic.status.msg_length = length;
  /* will set xfer_length when receiving the notify */

  ret = omx__submit_isend_rndv(ep, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_NEED_RETRY);
    omx__debug_printf(SEND, "queueing large send request %p\n", req);
    req->generic.state = OMX_REQUEST_STATE_QUEUED;
    omx__enqueue_request(&ep->queued_send_req_q, req);
  }
}

/**************
 * Send Notify
 */

static INLINE void
omx__post_notify(struct omx_endpoint *ep,
		 struct omx__partner *partner,
		 union omx_request * req)
{
  struct omx_cmd_send_notify * notify_param = &req->recv.specific.large.send_notify_ioctl_param;
  omx__seqnum_t ack_upto = omx__get_partner_needed_ack(ep, partner);
  int err;

  omx__debug_printf(ACK, "piggy acking back to partner up to %d (#%d) at jiffies %lld\n",
		    (unsigned int) OMX__SEQNUM(ack_upto - 1),
		    (unsigned int) OMX__SESNUM_SHIFTED(ack_upto - 1),
		    (unsigned long long) omx__driver_desc->jiffies);
  notify_param->piggyack = ack_upto;

  err = ioctl(ep->fd, OMX_CMD_SEND_NOTIFY, notify_param);
  if (unlikely(err < 0)) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "send notify message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

void
omx__submit_notify(struct omx_endpoint *ep,
		   union omx_request *req)
{
  struct omx_cmd_send_notify * notify_param;
  struct omx__partner * partner;
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  omx__seqnum_t seqnum;

  partner = req->generic.partner;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);

  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  notify_param = &req->recv.specific.large.send_notify_ioctl_param;
  notify_param->peer_index = partner->peer_index;
  notify_param->dest_endpoint = partner->endpoint_index;
  notify_param->shared = omx__partner_localization_shared(partner);
  notify_param->total_length = req->generic.status.xfer_length;
  notify_param->session_id = partner->back_session_id;
  notify_param->seqnum = seqnum;
  notify_param->puller_rdma_id = req->recv.specific.large.target_rdma_id;
  notify_param->puller_rdma_seqnum = req->recv.specific.large.target_rdma_seqnum;

  omx__post_notify(ep, partner, req);

  /* no need to wait for a done event, tiny is synchronous */
  req->generic.state |= OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_partner_non_acked_request(partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);
}

void
omx__queue_notify(struct omx_endpoint *ep,
		  union omx_request *req)
{
  req->generic.state |= OMX_REQUEST_STATE_QUEUED;
  omx__enqueue_request(&ep->queued_send_req_q, req);
}

/****************************
 * ISEND Submission Routines
 */

static INLINE void
omx__isend_req(struct omx_endpoint *ep, struct omx__partner *partner,
	       union omx_request *req, union omx_request **requestp)
{
  uint32_t length = req->send.segs.total_length;

  omx__debug_printf(SEND, "sending %ld bytes in %d segments using seqnum %d (#%d)\n",
		    (unsigned long) length, (unsigned) req->send.segs.nseg,
		    (unsigned) OMX__SEQNUM(partner->next_send_seq),
		    (unsigned) OMX__SESNUM_SHIFTED(partner->next_send_seq));

#ifndef OMX_DISABLE_SELF
  if (unlikely(omx__globals.selfcomms && partner == ep->myself)) {
    omx__process_self_send(ep, req);
  } else
#endif
  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state = OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    req->throttling.ssend = 0;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);

  } else if (likely(length <= OMX_TINY_MAX)) {
    omx__submit_or_queue_isend_tiny(ep, req, partner);
  } else if (length <= OMX_SMALL_MAX) {
    omx__submit_or_queue_isend_small(ep, req, partner);
  } else if (length <= OMX_MEDIUM_MAX) {
    omx__submit_or_queue_isend_medium(ep, req, partner);
  } else {
    omx__submit_or_queue_isend_large(ep, req, partner);
  }

  if (requestp) {
    *requestp = req;
  } else {
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
    ep->zombies++;
  }

  /* progress a little bit */
  omx__progress(ep);
}

/* API omx_isend */
omx_return_t
omx_isend(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  omx_endpoint_addr_t dest_endpoint,
	  uint64_t match_info,
	  void *context, union omx_request **requestp)
{
  struct omx__partner *partner;
  union omx_request *req;
  omx_return_t ret = OMX_SUCCESS;

  OMX__ENDPOINT_LOCK(ep);

  req = omx__request_alloc(ep);
  if (unlikely(!req)) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating isend request");
    goto out_with_lock;
  }

  omx_cache_single_segment(&req->send.segs, buffer, length);

  req->generic.partner = partner = omx__partner_from_addr(&dest_endpoint);
  req->generic.status.addr = dest_endpoint;
  req->generic.status.match_info = match_info;
  req->generic.status.context = context;

  omx__isend_req(ep, partner, req, requestp);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/* API omx_isendv */
omx_return_t
omx_isendv(omx_endpoint_t ep,
	   omx_seg_t *segs, uint32_t nseg,
	   omx_endpoint_addr_t dest_endpoint,
	   uint64_t match_info,
	   void * context, omx_request_t * requestp)
{
  struct omx__partner *partner;
  union omx_request *req;
  omx_return_t ret;

  OMX__ENDPOINT_LOCK(ep);

  req = omx__request_alloc(ep);
  if (unlikely(!req)) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating vectorial isend request");
    goto out_with_lock;
  }

  ret = omx_cache_segments(&req->send.segs, segs, nseg);
  if (unlikely(ret != OMX_SUCCESS)) {
    /* the callee let us check errors */
    ret = omx__error_with_ep(ep, ret,
			     "Allocating %ld-vectorial isend request segment array",
			     (unsigned long long) nseg);
    omx__request_free(ep, req);
    goto out_with_lock;
  }

  req->generic.partner = partner = omx__partner_from_addr(&dest_endpoint);
  req->generic.status.addr = dest_endpoint;
  req->generic.status.match_info = match_info;
  req->generic.status.context = context;

  omx__isend_req(ep, partner, req, requestp);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/*****************************
 * ISSEND Submission Routines
 */

static INLINE void
omx__issend_req(struct omx_endpoint *ep, struct omx__partner *partner,
		union omx_request *req,	union omx_request **requestp)
{
  omx__debug_printf(SEND, "ssending %ld bytes in %d segments using seqnum %d (#%d)\n",
		    (unsigned long) req->send.segs.total_length, (unsigned) req->send.segs.nseg,
		    (unsigned) OMX__SEQNUM(partner->next_send_seq),
		    (unsigned) OMX__SESNUM_SHIFTED(partner->next_send_seq));

#ifndef OMX_DISABLE_SELF
  if (unlikely(omx__globals.selfcomms && partner == ep->myself)) {
    omx__process_self_send(ep, req);
  } else
#endif
  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state = OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    req->throttling.ssend = 1;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);

  } else {
    omx__submit_or_queue_isend_large(ep, req, partner);
  }

  if (requestp) {
    *requestp = req;
  } else {
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
    ep->zombies++;
  }

  /* progress a little bit */
  omx__progress(ep);
}

/* API omx_issend */
omx_return_t
omx_issend(struct omx_endpoint *ep,
	   void *buffer, size_t length,
	   omx_endpoint_addr_t dest_endpoint,
	   uint64_t match_info,
	   void *context, union omx_request **requestp)
{
  struct omx__partner *partner;
  union omx_request *req;
  omx_return_t ret = OMX_SUCCESS;

  OMX__ENDPOINT_LOCK(ep);

  req = omx__request_alloc(ep);
  if (unlikely(!req)) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating issend request");
    goto out_with_lock;
  }

  omx_cache_single_segment(&req->send.segs, buffer, length);

  req->generic.partner = partner = omx__partner_from_addr(&dest_endpoint);
  req->generic.status.addr = dest_endpoint;
  req->generic.status.match_info = match_info;
  req->generic.status.context = context;

  omx__issend_req(ep, partner, req, requestp);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/* API omx_issendv */
omx_return_t
omx_issendv(omx_endpoint_t ep,
	    omx_seg_t *segs, uint32_t nseg,
	    omx_endpoint_addr_t dest_endpoint,
	    uint64_t match_info,
	    void * context, omx_request_t * requestp)
{
  struct omx__partner *partner;
  union omx_request *req;
  omx_return_t ret;

  OMX__ENDPOINT_LOCK(ep);

  req = omx__request_alloc(ep);
  if (unlikely(!req)) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating vectorial issend request");
    goto out_with_lock;
  }

  ret = omx_cache_segments(&req->send.segs, segs, nseg);
  if (unlikely(ret != OMX_SUCCESS)) {
    /* the callee let us check errors */
    ret = omx__error_with_ep(ep, ret,
			     "Allocating %ld-vectorial issend request segment array",
			     (unsigned long long) nseg);
    omx__request_free(ep, req);
    goto out_with_lock;
  }

  req->generic.partner = partner = omx__partner_from_addr(&dest_endpoint);
  req->generic.status.addr = dest_endpoint;
  req->generic.status.match_info = match_info;
  req->generic.status.context = context;

  omx__issend_req(ep, partner, req, requestp);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/***********************
 * Send Queued Requests
 */

void
omx__process_queued_requests(struct omx_endpoint *ep)
{
  union omx_request *req, *next;

  omx__foreach_request_safe(&ep->queued_send_req_q, req, next) {
    omx_return_t ret;

    req->generic.state &= ~OMX_REQUEST_STATE_QUEUED;
    omx___dequeue_request(req);

    switch (req->generic.type) {
    case OMX_REQUEST_TYPE_SEND_SMALL:
      omx__debug_printf(SEND, "trying to resubmit queued send small request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      ret = omx__submit_isend_small(ep, req);
      break;
    case OMX_REQUEST_TYPE_SEND_MEDIUM:
      omx__debug_printf(SEND, "trying to resubmit queued send medium request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      ret = omx__submit_isend_medium(ep, req);
      break;
    case OMX_REQUEST_TYPE_SEND_LARGE:
      omx__debug_printf(SEND, "trying to resubmit queued send large request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      ret = omx__submit_isend_rndv(ep, req);
      break;
    case OMX_REQUEST_TYPE_RECV_LARGE:
      if (req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL) {
	/* if partial, we need to post the pull request to the driver */
	omx__debug_printf(SEND, "trying to resubmit queued recv large request %p seqnum %d (#%d)\n", req,
			  (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			  (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
	ret = omx__submit_pull(ep, req);
      } else {
	/* if not partial, the pull is already done, we need to send the notify */
	omx__debug_printf(SEND, "trying to resubmit queued recv large request notify message %p seqnum %d (#%d)\n", req,
			  (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			  (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
	omx__submit_notify(ep, req);
	ret = OMX_SUCCESS;
      }
      break;
    default:
      omx__abort("Failed to handle queued request with type %d\n",
		 req->generic.type);
    }

    if (unlikely(ret != OMX_SUCCESS)) {
      omx__debug_assert(ret == OMX_INTERNAL_NEED_RETRY);
      /* put back at the head of the queue */
      omx__debug_printf(SEND, "requeueing back queued request %p\n", req);
      req->generic.state |= OMX_REQUEST_STATE_QUEUED;
      omx__requeue_request(&ep->queued_send_req_q, req);
      break;
    }
  }
}

/***************************
 * Send throttling Requests
 */

void
omx__send_throttling_requests(struct omx_endpoint *ep, struct omx__partner *partner, int nr)
{
  union omx_request *req;

  while (nr && (req = omx__dequeue_first_partner_throttling_request(partner)) != NULL) {
    uint32_t length = req->send.segs.total_length;

    omx__debug_assert(req->generic.state == OMX_REQUEST_STATE_SEND_NEED_SEQNUM);
    req->generic.state = 0;

    if (req->throttling.ssend) {
      omx__submit_or_queue_isend_large(ep, req, partner);
    } else if (likely(length <= OMX_TINY_MAX)) {
      omx__submit_or_queue_isend_tiny(ep, req, partner);
    } else if (length <= OMX_SMALL_MAX) {
      omx__submit_or_queue_isend_small(ep, req, partner);
    } else if (length <= OMX_MEDIUM_MAX) {
      omx__submit_or_queue_isend_medium(ep, req, partner);
    } else {
      omx__submit_or_queue_isend_large(ep, req, partner);
    }

    omx__mark_partner_not_throttling(ep, partner);
    nr--;
  }
}

/******************
 * Resend messages
 */

void
omx__process_resend_requests(struct omx_endpoint *ep)
{
  union omx_request *req, *next;
  uint64_t now = omx__driver_desc->jiffies;

  /* move non acked requests to the requeued_send_req_q */
  omx__foreach_request_safe(&ep->non_acked_req_q, req, next) {
    if (now - req->generic.last_send_jiffies < omx__globals.resend_delay_jiffies)
      /* the remaining ones are more recent, no need to resend them yet */
      break;

    omx___dequeue_request(req);
    req->generic.state |= OMX_REQUEST_STATE_REQUEUED;
    omx__enqueue_request(&ep->requeued_send_req_q, req);
  }

  /* resend requests from the requeued_send_req_q */
  omx__foreach_request_safe(&ep->requeued_send_req_q, req, next) {
    /* check before dequeueing so that omx__partner_cleanup() is called with queues in a coherent state */
    if (req->generic.resends > req->generic.resends_max) {
      /* Disconnect the peer (and drop the requests) */
      printf("Send request (seqnum %d sesnum %d)  timeout, already sent %ld times, resetting partner status\n",
	     (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
	     (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum),
	     (unsigned long) req->generic.resends);
      omx__partner_cleanup(ep, req->generic.partner, 1);
      continue;
    }

    req->generic.state &= ~OMX_REQUEST_STATE_REQUEUED;
    omx___dequeue_request(req);

    switch (req->generic.type) {
    case OMX_REQUEST_TYPE_SEND_TINY:
      omx__debug_printf(SEND, "reposting requeued send tiny request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_tiny(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_SMALL:
      omx__debug_printf(SEND, "reposting requeued send small request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_small(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_MEDIUM:
      omx__debug_printf(SEND, "reposting requeued medium small request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_medium(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_LARGE:
      omx__debug_printf(SEND, "reposting requeued send rndv request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_rndv(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_RECV_LARGE:
      omx__debug_printf(SEND, "reposting requeued send notify request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_notify(ep, req->generic.partner, req);
      break;
    default:
      omx__abort("Failed to handle requeued request with type %d\n",
		 req->generic.type);
    }
  }

  /* resend non-replied connect requests */
  omx__foreach_request_safe(&ep->connect_req_q, req, next) {
    if (now - req->generic.last_send_jiffies < omx__globals.resend_delay_jiffies)
      /* the remaining ones are more recent, no need to resend them yet */
      break;

    /* check before dequeueing so that omx__partner_cleanup() is called with queues in a coherent state */
    if (req->generic.resends > req->generic.resends_max) {
      /* Disconnect the peer (and drop the requests) */
      printf("Connect request (connect seqnum %d) timeout, already sent %ld times, resetting partner status\n",
	     (unsigned int) req->connect.connect_seqnum,
	     (unsigned long) req->generic.resends);
      omx__partner_cleanup(ep, req->generic.partner, 1);
      continue;
    }

    /* no need to dequeue/requeue */
    omx__post_connect_request(ep, req->generic.partner, req);
  }
}
