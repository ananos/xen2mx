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

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE void
omx__setup_isend_tiny(struct omx_endpoint *ep,
		      struct omx__partner *partner,
		      union omx_request * req)
{
  struct omx_cmd_send_tiny * tiny_param = &req->send.specific.tiny.send_tiny_ioctl_param;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);
  omx__seqnum_t seqnum;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  tiny_param->hdr.seqnum = seqnum;

  omx__post_isend_tiny(ep, partner, req);

  req->generic.state |= OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);
}

static INLINE void
omx__alloc_setup_isend_tiny(struct omx_endpoint *ep,
			    struct omx__partner *partner,
			    union omx_request * req)
{
  struct omx_cmd_send_tiny * tiny_param;
  uint32_t length = req->generic.status.msg_length;

  tiny_param = &req->send.specific.tiny.send_tiny_ioctl_param;
  tiny_param->hdr.peer_index = partner->peer_index;
  tiny_param->hdr.dest_endpoint = partner->endpoint_index;
  tiny_param->hdr.shared = omx__partner_localization_shared(partner);
  tiny_param->hdr.match_info = req->generic.status.match_info;
  tiny_param->hdr.length = length;
  tiny_param->hdr.session_id = partner->true_session_id;
  omx_copy_from_segments(tiny_param->data, &req->send.segs, length);

  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state |= OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);
  } else {
    omx__setup_isend_tiny(ep, partner, req);
  }
}

static INLINE void
omx__submit_isend_tiny(struct omx_endpoint *ep,
		       struct omx__partner * partner,
		       union omx_request * req)
{
  uint32_t length = req->send.segs.total_length;

  req->generic.type = OMX_REQUEST_TYPE_SEND_TINY;
  /* no resources needed */

  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  if (unlikely(!omx__empty_queue(&ep->delayed_send_req_q))) {
    omx__alloc_setup_isend_tiny(ep, partner, req);
  } else {
    /* some requests are delayed, do not submit, queue as well */
    omx__debug_printf(SEND, "delaying send tiny request %p\n", req);
    req->generic.state |= OMX_REQUEST_STATE_DELAYED;
    omx__enqueue_request(&ep->delayed_send_req_q, req);
  }
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

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE void
omx__setup_isend_small(struct omx_endpoint *ep,
		       struct omx__partner *partner,
		       union omx_request * req)
{
  struct omx_cmd_send_small * small_param = &req->send.specific.small.send_small_ioctl_param;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);
  omx__seqnum_t seqnum;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  small_param->seqnum = seqnum;

  omx__post_isend_small(ep, partner, req);

  req->generic.state |= OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);
}

static INLINE omx_return_t
omx__alloc_setup_isend_small(struct omx_endpoint *ep,
			     struct omx__partner * partner,
			     union omx_request *req)
{
  struct omx_cmd_send_small * small_param;
  uint32_t length = req->generic.status.msg_length;
  void *copy = req->send.specific.small.copy;

  small_param = &req->send.specific.small.send_small_ioctl_param;
  small_param->peer_index = partner->peer_index;
  small_param->dest_endpoint = partner->endpoint_index;
  small_param->shared = omx__partner_localization_shared(partner);
  small_param->match_info = req->generic.status.match_info;
  small_param->length = length;
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

  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state |= OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);
  } else {
    omx__setup_isend_small(ep, partner, req);
  }

  /* bufferize data for retransmission (if not done already) */
  if (likely(req->send.segs.nseg == 1)) {
    omx_copy_from_segments(copy, &req->send.segs, length);
    small_param->vaddr = (uintptr_t) copy;
  }

  return OMX_SUCCESS;
}

static INLINE void
omx__submit_isend_small(struct omx_endpoint *ep,
			struct omx__partner * partner,
			union omx_request *req)
{
  uint32_t length = req->send.segs.total_length;
  omx_return_t ret;

  req->generic.type = OMX_REQUEST_TYPE_SEND_SMALL;
  /* no resources needed */

  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  if (unlikely(!omx__empty_queue(&ep->delayed_send_req_q)))
    /* some requests are delayed, do not submit, queue as well */
    goto delay;

  ret = omx__alloc_setup_isend_small(ep, partner, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
delay:
    omx__debug_printf(SEND, "delaying send small request %p\n", req);
    req->generic.state |= OMX_REQUEST_STATE_DELAYED;
    omx__enqueue_request(&ep->delayed_send_req_q, req);
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
      unsigned chunk = remaining > omx__globals.medium_frag_length
        ? omx__globals.medium_frag_length : remaining;
      medium_param->frag_length = chunk;
      medium_param->frag_seqnum = i;
      medium_param->sendq_page_offset = sendq_index[i];
      omx__debug_printf(MEDIUM, "sending medium seqnum %d pipeline 2 length %d of total %ld\n",
			i, chunk, (unsigned long) length);

      /* copy the data in the sendq only once */
      if (likely(!req->generic.resends))
	memcpy(ep->sendq + (sendq_index[i] << OMX_SENDQ_ENTRY_SHIFT), data + offset, chunk);

      err = ioctl(ep->fd, OMX_CMD_SEND_MEDIUM, medium_param);
      if (unlikely(err < 0)) {
	/* finish copying frags if not done already */
	if (likely(!req->generic.resends)) {
	  int j;
	  for(j=i+1; j<frags_nr; i++) {
	    unsigned chunk = remaining > omx__globals.medium_frag_length
	      ? omx__globals.medium_frag_length : remaining;
	    memcpy(ep->sendq + (sendq_index[j] << OMX_SENDQ_ENTRY_SHIFT), data + offset, chunk);
	    remaining -= chunk;
	    offset += chunk;
	  }
	}
	goto err;
      }

      remaining -= chunk;
      offset += chunk;
    }

  } else {
    /* initialize the state to the beginning */
    struct omx_segscan_state state = { .seg = &req->send.segs.segs[0], .offset = 0 };

    for(i=0; i<frags_nr; i++) {
      unsigned chunk = remaining > omx__globals.medium_frag_length
        ? omx__globals.medium_frag_length : remaining;
      medium_param->frag_length = chunk;
      medium_param->frag_seqnum = i;
      medium_param->sendq_page_offset = sendq_index[i];
      omx__debug_printf(MEDIUM, "sending medium seqnum %d pipeline 2 length %d of total %ld\n",
			i, chunk, (unsigned long) length);

      /* copy the data in the sendq only once */
      if (likely(!req->generic.resends))
	omx_continue_partial_copy_from_segments(ep->sendq + (sendq_index[i] << OMX_SENDQ_ENTRY_SHIFT),
						&req->send.segs, chunk,
						&state);

      err = ioctl(ep->fd, OMX_CMD_SEND_MEDIUM, medium_param);
      if (unlikely(err < 0)) {
	/* finish copying frags if not done already */
	if (likely(!req->generic.resends)) {
	  int j;
	  for(j=i+1; j<frags_nr; i++) {
	    unsigned chunk = remaining > omx__globals.medium_frag_length
	      ? omx__globals.medium_frag_length : remaining;
	    omx_continue_partial_copy_from_segments(ep->sendq + (sendq_index[j] << OMX_SENDQ_ENTRY_SHIFT),
						    &req->send.segs, chunk,
						    &state);
	    remaining -= chunk;
	  }
	}
	goto err;
      }

      remaining -= chunk;
    }
  }

  req->send.specific.medium.frags_pending_nr = frags_nr;

 ok:
  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  req->generic.state |= OMX_REQUEST_STATE_IN_DRIVER;

  /* at least one frag was posted, the ack has been sent for sure */
  omx__mark_partner_ack_sent(ep, partner);

  return;

 err:
  /* assume some frags got lost and let retransmission take care of it later */
  omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				     OMX_SUCCESS,
				     "send medium message fragment");

  /* update the number of fragment that we actually submitted */
  req->send.specific.medium.frags_pending_nr = i;
  ep->avail_exp_events += frags_nr - i;
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
}

static INLINE void
omx__setup_isend_medium(struct omx_endpoint *ep,
			struct omx__partner * partner,
			union omx_request *req)
{
  struct omx_cmd_send_medium * medium_param = &req->send.specific.medium.send_medium_ioctl_param;
  uint64_t match_info = req->generic.status.match_info;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  omx__seqnum_t seqnum;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  medium_param->seqnum = seqnum;

  omx__post_isend_medium(ep, partner, req);

  req->generic.state |= OMX_REQUEST_STATE_NEED_ACK;
  if (req->generic.state & OMX_REQUEST_STATE_IN_DRIVER)
    omx__enqueue_request(&ep->driver_posted_req_q, req);
  else
    omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);
}

static INLINE omx_return_t
omx__alloc_setup_isend_medium(struct omx_endpoint *ep,
			      struct omx__partner *partner,
			      union omx_request *req)
{
  struct omx_cmd_send_medium * medium_param = &req->send.specific.medium.send_medium_ioctl_param;
  uint32_t length = req->generic.status.msg_length;
  int * sendq_index = req->send.specific.medium.sendq_map_index;
  int res = req->generic.missing_resources;
  int frags_nr = req->send.specific.medium.frags_nr;

  if (likely(res & OMX_REQUEST_RESOURCE_EXP_EVENT))
    goto need_exp_events;
  if (likely(res & OMX_REQUEST_RESOURCE_SENDQ_SLOT))
    goto need_sendq_map_slot;
  omx__abort("Unexpected missing resources %x for medium send request\n", res);

 need_exp_events:
  if (unlikely(ep->avail_exp_events < frags_nr))
    return OMX_INTERNAL_MISSING_RESOURCES;
  ep->avail_exp_events -= frags_nr;
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_EXP_EVENT;

 need_sendq_map_slot:
  if (unlikely(omx__endpoint_sendq_map_get(ep, req->send.specific.medium.frags_nr, req, sendq_index) < 0))
    return OMX_INTERNAL_MISSING_RESOURCES;
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_SENDQ_SLOT;
  omx__debug_assert(!req->generic.missing_resources);

  medium_param->peer_index = partner->peer_index;
  medium_param->dest_endpoint = partner->endpoint_index;
  medium_param->shared = omx__partner_localization_shared(partner);
  medium_param->match_info = req->generic.status.match_info;
  medium_param->frag_pipeline = omx__globals.medium_frag_pipeline;
  medium_param->msg_length = length;
  medium_param->session_id = partner->true_session_id;

  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state |= OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);
  } else {
    omx__setup_isend_medium(ep, partner, req);
  }

  return OMX_SUCCESS;
}

static INLINE void
omx__submit_isend_medium(struct omx_endpoint *ep,
			 struct omx__partner * partner,
			 union omx_request *req)
{
  uint32_t length = req->send.segs.total_length;
  omx_return_t ret;
  int frags_nr;

  req->generic.type = OMX_REQUEST_TYPE_SEND_MEDIUM;
  req->generic.missing_resources = OMX_REQUEST_SEND_MEDIUM_RESOURCES;

  frags_nr = (length+omx__globals.medium_frag_length-1)>>omx__globals.medium_frag_pipeline;
  omx__debug_assert(frags_nr <= OMX_MEDIUM_FRAGS_MAX); /* for the sendq_index array above */
  req->send.specific.medium.frags_nr = frags_nr;

  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  if (unlikely(!omx__empty_queue(&ep->delayed_send_req_q)))
    /* some requests are delayed, do not submit, queue as well */
    goto delay;

  ret = omx__alloc_setup_isend_medium(ep, partner, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
delay:
    omx__debug_printf(SEND, "delaying send medium request %p\n", req);
    req->generic.state |= OMX_REQUEST_STATE_DELAYED;
    omx__enqueue_request(&ep->delayed_send_req_q, req);
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

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE void
omx__setup_isend_rndv(struct omx_endpoint *ep,
		      struct omx__partner *partner,
		      union omx_request * req)
{
  struct omx_cmd_send_rndv * rndv_param = &req->send.specific.large.send_rndv_ioctl_param;
  omx__seqnum_t seqnum;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  rndv_param->hdr.seqnum = req->generic.send_seqnum;

  omx__post_isend_rndv(ep, partner, req);

  req->generic.state |= OMX_REQUEST_STATE_NEED_REPLY|OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  /* cannot mark as done early since data is not buffered */
}

static INLINE omx_return_t
omx__alloc_setup_isend_large(struct omx_endpoint *ep,
			     struct omx__partner * partner,
			     union omx_request *req)
{
  struct omx_cmd_send_rndv * rndv_param = &req->send.specific.large.send_rndv_ioctl_param;
  struct omx__rndv_data * data_n = (void *) rndv_param->data;
  struct omx__large_region *region;
  uint32_t length = req->generic.status.msg_length;
  int res = req->generic.missing_resources;
  omx_return_t ret;

  if (likely(res & OMX_REQUEST_RESOURCE_SEND_LARGE_REGION))
    goto need_send_large_region;
  if (likely(res & OMX_REQUEST_RESOURCE_LARGE_REGION))
    goto need_large_region;
  omx__abort("Unexpected missing resources %x for large send request\n", res);

 need_send_large_region:
  if (unlikely(!ep->large_sends_avail_nr))
    return OMX_INTERNAL_MISSING_RESOURCES;
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_SEND_LARGE_REGION;
  ep->large_sends_avail_nr--;

 need_large_region:
  ret = omx__get_region(ep, &req->send.segs, &region, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
    return ret;
  }
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_LARGE_REGION;
  omx__debug_assert(!req->generic.missing_resources);

  req->send.specific.large.region = region;
  req->send.specific.large.region_seqnum = region->last_seqnum++;

  rndv_param->hdr.peer_index = partner->peer_index;
  rndv_param->hdr.dest_endpoint = partner->endpoint_index;
  rndv_param->hdr.shared = omx__partner_localization_shared(partner);
  rndv_param->hdr.match_info = req->generic.status.match_info;
  rndv_param->hdr.length = sizeof(struct omx__rndv_data);
  rndv_param->hdr.session_id = partner->true_session_id;
  rndv_param->hdr.user_region_id_needed = region->id;
  rndv_param->hdr.user_region_length_needed = region->offset + length;

  OMX_PKT_FIELD_FROM(data_n->msg_length, length);
  OMX_PKT_FIELD_FROM(data_n->rdma_id, region->id);
  OMX_PKT_FIELD_FROM(data_n->rdma_seqnum, req->send.specific.large.region_seqnum);
  OMX_PKT_FIELD_FROM(data_n->rdma_offset, region->offset);

  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state |= OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);
  } else {
    omx__setup_isend_rndv(ep, partner, req);
  }

  return OMX_SUCCESS;
}

static INLINE void
omx__submit_isend_large(struct omx_endpoint *ep,
			struct omx__partner * partner,
			union omx_request *req)
{
  uint32_t length = req->send.segs.total_length;
  omx_return_t ret;

  req->generic.type = OMX_REQUEST_TYPE_SEND_LARGE;
  req->generic.missing_resources = OMX_REQUEST_SEND_LARGE_RESOURCES;

  req->generic.status.msg_length = length;
  /* will set xfer_length when receiving the notify */

  if (unlikely(!omx__empty_queue(&ep->delayed_send_req_q)))
    /* some requests are delayed, do not submit, queue as well */
    goto delay;

  ret = omx__alloc_setup_isend_large(ep, partner, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
delay:
    omx__debug_printf(SEND, "delaying large send request %p\n", req);
    req->generic.state |= OMX_REQUEST_STATE_DELAYED;
    omx__enqueue_request(&ep->delayed_send_req_q, req);
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

  if (!err)
    omx__mark_partner_ack_sent(ep, partner);
}

static INLINE void
omx__setup_notify(struct omx_endpoint *ep,
		  struct omx__partner *partner,
		  union omx_request * req)
{
  struct omx_cmd_send_notify * notify_param = &req->recv.specific.large.send_notify_ioctl_param;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);
  omx__seqnum_t seqnum;

  seqnum = partner->next_send_seq;
  OMX__SEQNUM_INCREASE(partner->next_send_seq);
  req->generic.send_seqnum = seqnum;
  req->generic.resends = 0;
  req->generic.resends_max = ep->req_resends_max;

  notify_param->seqnum = seqnum;

  omx__post_notify(ep, partner, req);

  req->generic.state |= OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  /* mark the request as done now, it will be resent/zombified later if necessary */
  omx__notify_request_done_early(ep, ctxid, req);
}

static INLINE void
omx__alloc_setup_notify(struct omx_endpoint *ep,
			union omx_request *req)
{
  struct omx_cmd_send_notify * notify_param = &req->recv.specific.large.send_notify_ioctl_param;
  struct omx__partner * partner = req->generic.partner;

  notify_param->peer_index = partner->peer_index;
  notify_param->dest_endpoint = partner->endpoint_index;
  notify_param->shared = omx__partner_localization_shared(partner);
  notify_param->total_length = req->generic.status.xfer_length;
  notify_param->session_id = partner->back_session_id;
  notify_param->puller_rdma_id = req->recv.specific.large.target_rdma_id;
  notify_param->puller_rdma_seqnum = req->recv.specific.large.target_rdma_seqnum;

  if (unlikely(OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq) >= OMX__THROTTLING_OFFSET_MAX)) {
    /* throttling */
    req->generic.state |= OMX_REQUEST_STATE_SEND_NEED_SEQNUM;
    omx__enqueue_partner_throttling_request(partner, req);
    omx__mark_partner_throttling(ep, partner);
  } else {
    omx__setup_notify(ep, partner, req);
  }
}

void
omx__submit_notify(struct omx_endpoint *ep,
		   union omx_request *req,
		   int delayed)
{
  /* type, xfer_length and msg_length already set when the large recv got matched */
  /* no resources needed, just need to wait for all events to be processed */

  if (unlikely(!omx__empty_queue(&ep->delayed_send_req_q) || delayed)) {
    req->generic.state |= OMX_REQUEST_STATE_DELAYED;
    /* queue on top of the delayed queue to avoid being blocked by delayed requests */
    omx__requeue_request(&ep->delayed_send_req_q, req);
  } else {
    omx__alloc_setup_notify(ep, req);
  }
}

/****************************
 * ISEND Submission Routines
 */

static INLINE omx_return_t
omx__isend_req(struct omx_endpoint *ep, struct omx__partner *partner,
	       union omx_request *req, union omx_request **requestp)
{
  uint32_t length = req->send.segs.total_length;

  omx__debug_printf(SEND, "sending %ld bytes in %d segments using seqnum %d (#%d)\n",
		    (unsigned long) length, (unsigned) req->send.segs.nseg,
		    (unsigned) OMX__SEQNUM(partner->next_send_seq),
		    (unsigned) OMX__SESNUM_SHIFTED(partner->next_send_seq));

  if (unlikely(!requestp))
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;

#ifndef OMX_DISABLE_SELF
  if (unlikely(omx__globals.selfcomms && partner == ep->myself)) {
    omx__process_self_send(ep, req);
  } else
#endif

  if (likely(length <= OMX_TINY_MAX)) {
    omx__submit_isend_tiny(ep, partner, req);
  } else if (length <= OMX_SMALL_MAX) {
    void *copy = malloc(length);
    if (unlikely(!copy))
      return omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating isend small copy buffer");
    req->send.specific.small.copy = copy;
    omx__submit_isend_small(ep, partner, req);
  } else if (length <= partner->rndv_threshold) {
    omx__submit_isend_medium(ep, partner, req);
  } else {
    omx__submit_isend_large(ep, partner, req);
  }

  if (requestp) {
    *requestp = req;
  } else {
    ep->zombies++;
  }

  /* progress a little bit */
  omx__progress(ep);

  return OMX_SUCCESS;
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

  ret = omx__isend_req(ep, partner, req, requestp);
  if (likely(ret != OMX_SUCCESS)) {
    omx_free_segments(&req->send.segs);
    omx__request_free(ep, req);
  }

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

  ret = omx__isend_req(ep, partner, req, requestp);
  if (likely(ret != OMX_SUCCESS)) {
    omx_free_segments(&req->send.segs);
    omx__request_free(ep, req);
  }

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

  if (unlikely(!requestp))
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;

#ifndef OMX_DISABLE_SELF
  if (unlikely(omx__globals.selfcomms && partner == ep->myself)) {
    omx__process_self_send(ep, req);
  } else
#endif
    omx__submit_isend_large(ep, partner, req);

  if (requestp) {
    *requestp = req;
  } else {
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

/*******************
 * Delayed Requests
 */

void
omx__process_delayed_requests(struct omx_endpoint *ep)
{
  union omx_request *req, *next;

  omx__foreach_request_safe(&ep->delayed_send_req_q, req, next) {
    omx_return_t ret;

    req->generic.state &= ~OMX_REQUEST_STATE_DELAYED;
    omx___dequeue_request(req);

    switch (req->generic.type) {
    case OMX_REQUEST_TYPE_SEND_TINY:
      omx__debug_printf(SEND, "trying to resubmit delayed send tiny request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__alloc_setup_isend_tiny(ep, req->generic.partner, req);
      ret = OMX_SUCCESS;
      break;
    case OMX_REQUEST_TYPE_SEND_SMALL:
      omx__debug_printf(SEND, "trying to resubmit delayed send small request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      ret = omx__alloc_setup_isend_small(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_MEDIUM:
      omx__debug_printf(SEND, "trying to resubmit delayed send medium request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      ret = omx__alloc_setup_isend_medium(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_LARGE:
      omx__debug_printf(SEND, "trying to resubmit delayed send large request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      ret = omx__alloc_setup_isend_large(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_RECV_LARGE:
      if (req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL) {
	/* if partial, we need to post the pull request to the driver */
	omx__debug_printf(SEND, "trying to resubmit delayed recv large request %p seqnum %d (#%d)\n", req,
			  (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			  (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
	ret = omx__alloc_setup_pull(ep, req);
      } else {
	/* if not partial, the pull is already done, we need to send the notify */
	omx__debug_printf(SEND, "trying to resubmit delayed recv large request notify message %p seqnum %d (#%d)\n", req,
			  (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			  (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
	omx__alloc_setup_notify(ep, req);
	ret = OMX_SUCCESS;
      }
      break;
    default:
      omx__abort("Failed to handle delayed request with type %d\n",
		 req->generic.type);
    }

    if (unlikely(ret != OMX_SUCCESS)) {
      omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
      /* put back at the head of the queue */
      omx__debug_printf(SEND, "requeueing back delayed request %p\n", req);
      req->generic.state |= OMX_REQUEST_STATE_DELAYED;
      omx__requeue_request(&ep->delayed_send_req_q, req);
      break;
    }
  }
}

void
omx__process_throttling_requests(struct omx_endpoint *ep, struct omx__partner *partner, int nr)
{
  union omx_request *req;
  int sent = 0;

  while (nr > sent && (req = omx__dequeue_first_partner_throttling_request(partner)) != NULL) {
    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_SEND_NEED_SEQNUM);
    req->generic.state &= ~OMX_REQUEST_STATE_SEND_NEED_SEQNUM;

    switch (req->generic.type) {
    case OMX_REQUEST_TYPE_SEND_TINY:
      omx__setup_isend_tiny(ep, partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_SMALL:
      omx__setup_isend_small(ep, partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_MEDIUM:
      omx__setup_isend_medium(ep, partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_LARGE:
      omx__setup_isend_rndv(ep, partner, req);
      break;
    case OMX_REQUEST_TYPE_RECV_LARGE:
      omx__setup_notify(ep, partner, req);
      break;
    default:
      omx__abort("Unexpected throttling request type %d\n", req->generic.type);
    }

    sent++;
  }

  omx__update_partner_throttling(ep, partner, sent);
}

/*
 * Cleanup send request state so that subsequent send/recv_complete doesn't break
 */
static INLINE void
omx__release_delayed_send_resources(struct omx_endpoint *ep, union omx_request *req)
{
  int res = req->generic.missing_resources;

  switch (req->generic.type) {

  case OMX_REQUEST_TYPE_SEND_MEDIUM:
    if (!(res & OMX_REQUEST_RESOURCE_EXP_EVENT))
      ep->avail_exp_events += req->send.specific.medium.frags_nr;

    /* make sure we don't release garbage sendq map slots */
    if (res & OMX_REQUEST_RESOURCE_SENDQ_SLOT)
      req->send.specific.medium.frags_nr = 0;

    break;

  case OMX_REQUEST_TYPE_SEND_LARGE:
    if (!(res & OMX_REQUEST_RESOURCE_SEND_LARGE_REGION))
      ep->large_sends_avail_nr++;

    if (!(res & OMX_REQUEST_RESOURCE_LARGE_REGION))
      omx__put_region(ep, req->send.specific.large.region, NULL);

    break;

  case OMX_REQUEST_TYPE_RECV_LARGE:
    if (req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL) {
      /* delayed before pull */

      if (!(res & OMX_REQUEST_RESOURCE_EXP_EVENT))
	ep->avail_exp_events++;

      if (!(res & OMX_REQUEST_RESOURCE_LARGE_REGION))
	omx__put_region(ep, req->recv.specific.large.local_region, NULL);

      /* nothing to do for OMX_REQUEST_RESOURCE_PULL_HANDLE */

    } else {
      /* delayed after pull before notify */

    }

    break;

  default:
    /* nothing to do */
    break;
  }
}

void
omx__complete_delayed_send_request(struct omx_endpoint *ep, union omx_request *req)
{
  switch (req->generic.type) {

  case OMX_REQUEST_TYPE_SEND_TINY:
  case OMX_REQUEST_TYPE_SEND_SMALL:
  case OMX_REQUEST_TYPE_SEND_MEDIUM:
  case OMX_REQUEST_TYPE_SEND_LARGE:
    omx__release_delayed_send_resources(ep, req);
    omx__send_complete(ep, req, OMX_REMOTE_ENDPOINT_UNREACHABLE);
    break;

  case OMX_REQUEST_TYPE_RECV_LARGE:
    if (req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL) {
      /* pull request needs to the pushed to the driver, no region allocated yet, just complete the request */
      req->generic.state &= OMX_REQUEST_STATE_RECV_PARTIAL;
    } else {
      /* the pull is already done, just drop the notify */
      omx__release_delayed_send_resources(ep, req);
    }
    omx__recv_complete(ep, req, OMX_REMOTE_ENDPOINT_UNREACHABLE);
    break;

  default:
    omx__abort("Failed to handle delayed request with type %d\n",
	       req->generic.type);
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

  /* resend the first requests from the non_acked queue */
  omx__foreach_request_safe(&ep->non_acked_req_q, req, next) {
    if (now - req->generic.last_send_jiffies < omx__globals.resend_delay_jiffies)
      /* the remaining ones are more recent, no need to resend them yet */
      break;

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

    omx___dequeue_request(req);

    switch (req->generic.type) {
    case OMX_REQUEST_TYPE_SEND_TINY:
      omx__debug_printf(SEND, "reposting resend tiny request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_tiny(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_SMALL:
      omx__debug_printf(SEND, "reposting resend small request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_small(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_MEDIUM:
      omx__debug_printf(SEND, "reposting resend medium request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      if (ep->avail_exp_events < req->send.specific.medium.frags_nr) {
	/* not enough expected events available, stop resending for now, and try again later */
	omx__debug_printf(SEND, "stopping resending for now, only %d exp events available to resend %d medium frags\n",
			  ep->avail_exp_events, req->send.specific.medium.frags_nr);
	omx__requeue_request(&ep->non_acked_req_q, req);
	goto done_resending;
      }
      ep->avail_exp_events -= req->send.specific.medium.frags_nr;
      omx__post_isend_medium(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_SEND_LARGE:
      omx__debug_printf(SEND, "reposting resend rndv request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_isend_rndv(ep, req->generic.partner, req);
      break;
    case OMX_REQUEST_TYPE_RECV_LARGE:
      omx__debug_printf(SEND, "reposting resend notify request %p seqnum %d (#%d)\n", req,
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx__post_notify(ep, req->generic.partner, req);
      break;
    default:
      omx__abort("Failed to handle resend request with type %d\n",
		 req->generic.type);
    }

    if (req->generic.state & OMX_REQUEST_STATE_IN_DRIVER)
      omx__enqueue_request(&ep->driver_posted_req_q, req);
    else
      /* move the request to the end of the same queue */
      omx__enqueue_request(&ep->non_acked_req_q, req);
  }
 done_resending:

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
