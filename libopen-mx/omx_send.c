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
#include "omx_request.h"
#include "omx_lib_wire.h"
#include "omx_wire_access.h"

/**************************
 * Send Request Completion
 */

void
omx__send_complete(struct omx_endpoint *ep, union omx_request *req,
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

  if (req->generic.type == OMX_REQUEST_TYPE_SEND_MEDIUM)
    omx__endpoint_sendq_map_put(ep, req->send.specific.medium.frags_nr, req->send.specific.medium.sendq_map_index);

  omx__enqueue_request(&ep->ctxid[ctxid].done_req_q, req);
}

/***********************************************
 * Low-level Send Request Posting to the Driver
 */

omx_return_t
omx__post_isend_tiny(struct omx_endpoint *ep,
		     struct omx__partner *partner,
		     union omx_request * req)
{
  struct omx_cmd_send_tiny * tiny_param = &req->send.specific.tiny.send_tiny_ioctl_param;
  int err;

  tiny_param->hdr.piggyack = partner->next_frag_recv_seq - 1;

  err = ioctl(ep->fd, OMX_CMD_SEND_TINY, tiny_param);
  if (unlikely(err < 0)) {
    omx_return_t ret = omx__errno_to_return("ioctl SEND_TINY");

    if (ret != OMX_NO_SYSTEM_RESOURCES)
      omx__abort("ioctl SEND_TINY returned unexpected error %m\n");

    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__partner_ack_sent(ep, partner);

  return OMX_SUCCESS;
}

omx_return_t
omx__post_isend_small(struct omx_endpoint *ep,
		      struct omx__partner *partner,
		      union omx_request * req)
{
  struct omx_cmd_send_small * small_param = &req->send.specific.small.send_small_ioctl_param;
  int err;

  small_param->piggyack = partner->next_frag_recv_seq - 1;

  err = ioctl(ep->fd, OMX_CMD_SEND_SMALL, small_param);
  if (unlikely(err < 0)) {
    omx_return_t ret = omx__errno_to_return("ioctl SEND_SMALL");

    if (ret != OMX_NO_SYSTEM_RESOURCES)
      omx__abort("ioctl SEND_SMALL returned unexpected error %m\n");

    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__partner_ack_sent(ep, partner);

  return OMX_SUCCESS;
}

omx_return_t
omx__post_isend_rndv(struct omx_endpoint *ep,
		     struct omx__partner *partner,
		     union omx_request * req)
{
  struct omx_cmd_send_rndv * rndv_param = &req->send.specific.large.send_rndv_ioctl_param;
  int err;

  rndv_param->hdr.piggyack = partner->next_frag_recv_seq - 1;

  err = ioctl(ep->fd, OMX_CMD_SEND_RNDV, rndv_param);
  if (unlikely(err < 0)) {
    omx_return_t ret = omx__errno_to_return("ioctl SEND_RNDV");

    if (ret != OMX_NO_SYSTEM_RESOURCES)
      omx__abort("ioctl SEND_RNDV returned unexpected error %m\n");

    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__partner_ack_sent(ep, partner);

  return OMX_SUCCESS;
}

omx_return_t
omx__post_isend_notify(struct omx_endpoint *ep,
		       struct omx__partner *partner,
		       union omx_request * req)
{
  struct omx_cmd_send_notify * notify_param = &req->recv.specific.large.send_notify_ioctl_param;
  int err;

  notify_param->piggyack = partner->next_frag_recv_seq - 1;

  err = ioctl(ep->fd, OMX_CMD_SEND_NOTIFY, notify_param);
  if (unlikely(err < 0)) {
    omx_return_t ret = omx__errno_to_return("ioctl SEND_NOTIFY");

    if (ret != OMX_NO_SYSTEM_RESOURCES)
      omx__abort("ioctl SEND_NOTIFY returned unexpected error %m\n");

    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__partner_ack_sent(ep, partner);

  return OMX_SUCCESS;
}

/****************************************************************
 * Internal Allocation, Submission and Queueing of Send Requests
 */

static INLINE omx_return_t
omx__submit_or_queue_isend_tiny(struct omx_endpoint *ep,
				void *buffer, size_t length,
				struct omx__partner * partner, omx__seqnum_t seqnum,
				uint64_t match_info,
				void *context, union omx_request **requestp)
{
  union omx_request * req;
  struct omx_cmd_send_tiny * tiny_param;

  req = omx__request_alloc(OMX_REQUEST_TYPE_SEND_TINY);
  if (unlikely(!req))
    return OMX_NO_RESOURCES;

  tiny_param = &req->send.specific.tiny.send_tiny_ioctl_param;
  tiny_param->hdr.peer_index = partner->peer_index;
  tiny_param->hdr.dest_endpoint = partner->endpoint_index;
  tiny_param->hdr.match_info = match_info;
  tiny_param->hdr.length = length;
  tiny_param->hdr.seqnum = seqnum;
  tiny_param->hdr.session_id = partner->session_id;
  memcpy(tiny_param->data, buffer, length);

  omx__post_isend_tiny(ep, partner, req);

  /* no need to wait for a done event, tiny is synchronous */
  req->generic.state = OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_partner_non_acked_request(partner, req);

  req->generic.partner = partner;
  omx__partner_to_addr(partner, &req->generic.status.addr);
  req->generic.send_seqnum = seqnum;
  req->generic.status.context = context;
  req->generic.status.match_info = match_info;
  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  *requestp = req;
  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__submit_or_queue_isend_small(struct omx_endpoint *ep,
				 void *buffer, size_t length,
				 struct omx__partner * partner, omx__seqnum_t seqnum,
				 uint64_t match_info,
				 void *context, union omx_request **requestp)
{
  union omx_request * req;
  struct omx_cmd_send_small * small_param;

  req = omx__request_alloc(OMX_REQUEST_TYPE_SEND_SMALL);
  if (unlikely(!req))
    return OMX_NO_RESOURCES;

  small_param = &req->send.specific.small.send_small_ioctl_param;
  small_param->peer_index = partner->peer_index;
  small_param->dest_endpoint = partner->endpoint_index;
  small_param->match_info = match_info;
  small_param->length = length;
  small_param->vaddr = (uintptr_t) buffer;
  small_param->seqnum = seqnum;
  small_param->session_id = partner->session_id;

  /* FIXME: bufferize data */

  omx__post_isend_small(ep, partner, req);

  /* no need to wait for a done event, small is synchronous */
  req->generic.state = OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_partner_non_acked_request(partner, req);

  req->generic.partner = partner;
  omx__partner_to_addr(partner, &req->generic.status.addr);
  req->generic.send_seqnum = seqnum;
  req->generic.status.context = context;
  req->generic.status.match_info = match_info;
  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  *requestp = req;
  return OMX_SUCCESS;
}

omx_return_t
omx__submit_isend_medium(struct omx_endpoint *ep,
			 union omx_request *req)
{
  struct omx_cmd_send_medium * medium_param = &req->send.specific.medium.send_medium_ioctl_param;
  struct omx__partner * partner = req->generic.partner;
  void * buffer = req->send.specific.medium.buffer;
  uint32_t length = req->generic.status.xfer_length;
  uint32_t remaining = length;
  uint32_t offset = 0;
  omx_return_t ret;
  int * sendq_index = req->send.specific.medium.sendq_map_index;
  int frags_nr;
  int err;
  int i;

  frags_nr = OMX_MEDIUM_FRAGS_NR(length);
  omx__debug_assert(frags_nr <= 8); /* for the sendq_index array above */
  req->send.specific.medium.frags_pending_nr = frags_nr;
  req->send.specific.medium.frags_nr = frags_nr;

  if (unlikely(ep->avail_exp_events < frags_nr
	       || omx__endpoint_sendq_map_get(ep, frags_nr, req, sendq_index) < 0))
    return OMX_NO_RESOURCES;

  medium_param->peer_index = partner->peer_index;
  medium_param->dest_endpoint = partner->endpoint_index;
  medium_param->match_info = req->generic.status.match_info;
  medium_param->frag_pipeline = OMX_MEDIUM_FRAG_PIPELINE;
  medium_param->msg_length = length;
  medium_param->seqnum = req->generic.send_seqnum;
  medium_param->piggyack = partner->next_frag_recv_seq - 1;
  medium_param->session_id = partner->session_id;

  for(i=0; i<frags_nr; i++) {
    unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
      ? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
    medium_param->frag_length = chunk;
    medium_param->frag_seqnum = i;
    medium_param->sendq_page_offset = sendq_index[i];
    omx__debug_printf("sending medium seqnum %d pipeline 2 length %d of total %ld\n",
		      i, chunk, (unsigned long) length);
    memcpy(ep->sendq + (sendq_index[i] << OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT), buffer + offset, chunk);

    err = ioctl(ep->fd, OMX_CMD_SEND_MEDIUM, medium_param);
    if (unlikely(err < 0)) {
      int posted = i;

      ret = omx__errno_to_return("ioctl SEND_MEDIUM");
      if (unlikely(ret != OMX_NO_SYSTEM_RESOURCES)) {
	omx__abort("Failed to post SEND MEDIUM, driver replied %m\n");
      }

      /* if some frags posted, behave as if other frags were lost */
      req->send.specific.medium.frags_pending_nr = posted;
      if (posted)
	goto posted;
      else {
	omx__endpoint_sendq_map_put(ep, frags_nr, sendq_index);
	return OMX_NO_SYSTEM_RESOURCES;
      }
    }

    ep->avail_exp_events--;
    remaining -= chunk;
    offset += chunk;
  }

 posted:
  omx__partner_ack_sent(ep, partner);
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
  req->generic.state = OMX_REQUEST_STATE_IN_DRIVER|OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_request(&ep->driver_posted_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__submit_or_queue_isend_medium(struct omx_endpoint *ep,
				  void *buffer, size_t length,
				  struct omx__partner * partner, omx__seqnum_t seqnum,
				  uint64_t match_info,
				  void *context, union omx_request **requestp)
{
  union omx_request * req;
  omx_return_t ret;

  req = omx__request_alloc(OMX_REQUEST_TYPE_SEND_MEDIUM);
  if (unlikely(!req)) {
    ret = OMX_NO_RESOURCES;
    goto out;
  }

  /* need to wait for a done event, since the sendq pages
   * might still be in use
   */
  req->generic.partner = partner;
  omx__partner_to_addr(partner, &req->generic.status.addr);
  req->generic.send_seqnum = seqnum;
  req->send.specific.medium.buffer = buffer;
  req->generic.status.context = context;
  req->generic.status.match_info = match_info;
  req->generic.status.msg_length = length;
  req->generic.status.xfer_length = length; /* truncation not notified to the sender */

  ret = omx__submit_isend_medium(ep, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_printf("queueing medium request %p\n", req);
    req->generic.state = OMX_REQUEST_STATE_QUEUED;
    omx__enqueue_request(&ep->queued_send_req_q, req);
  }

  *requestp = req;
  return OMX_SUCCESS;

 out:
  return ret;
}

omx_return_t
omx__submit_isend_rndv(struct omx_endpoint *ep,
		       union omx_request *req)
{
  struct omx_cmd_send_rndv * rndv_param = &req->send.specific.large.send_rndv_ioctl_param;
  struct omx__rndv_data * data_n = (void *) rndv_param->data;
  struct omx__large_region *region;
  struct omx__partner * partner = req->generic.partner;
  void * buffer = req->send.specific.large.buffer;
  uint32_t length = req->generic.status.msg_length;
  omx_return_t ret;

  ret = omx__get_region(ep, buffer, length, &region);
  if (unlikely(ret != OMX_SUCCESS))
    return ret;

  rndv_param->hdr.peer_index = partner->peer_index;
  rndv_param->hdr.dest_endpoint = partner->endpoint_index;
  rndv_param->hdr.match_info = req->generic.status.match_info;
  rndv_param->hdr.length = sizeof(struct omx__rndv_data);
  rndv_param->hdr.seqnum = req->generic.send_seqnum;
  rndv_param->hdr.session_id = partner->session_id;

  OMX_PKT_FIELD_FROM(data_n->msg_length, length);
  OMX_PKT_FIELD_FROM(data_n->rdma_id, region->id);
  OMX_PKT_FIELD_FROM(data_n->rdma_seqnum, region->seqnum);
  OMX_PKT_FIELD_FROM(data_n->rdma_offset, region->offset);

  omx__post_isend_rndv(ep, partner, req);

  /* no need to wait for a done event, tiny is synchronous */
  req->generic.state = OMX_REQUEST_STATE_NEED_REPLY|OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_partner_non_acked_request(partner, req);

  req->send.specific.large.region = region;
  region->user = req;

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__submit_or_queue_isend_large(struct omx_endpoint *ep,
				 void *buffer, size_t length,
				 struct omx__partner * partner, omx__seqnum_t seqnum,
				 uint64_t match_info,
				 void *context, union omx_request **requestp)
{
  union omx_request * req;
  omx_return_t ret;

  req = omx__request_alloc(OMX_REQUEST_TYPE_SEND_LARGE);
  if (unlikely(!req))
    return OMX_NO_RESOURCES;

  req->generic.partner = partner;
  omx__partner_to_addr(partner, &req->generic.status.addr);
  req->generic.send_seqnum = seqnum;
  req->send.specific.large.buffer = buffer;
  req->generic.status.context = context;
  req->generic.status.match_info = match_info;
  req->generic.status.msg_length = length;
  /* will set xfer_length when receiving the notify */

  ret = omx__submit_isend_rndv(ep, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_printf("queueing large send request %p\n", req);
    req->generic.state = OMX_REQUEST_STATE_QUEUED;
    omx__enqueue_request(&ep->queued_send_req_q, req);
  }

  *requestp = req;
  return OMX_SUCCESS;
}

/*************************************
 * API-Level Send Submission Routines
 */

omx_return_t
omx_isend(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  omx_endpoint_addr_t dest_endpoint,
	  uint64_t match_info,
	  void *context, union omx_request **requestp)
{
  struct omx__partner * partner;
  omx__seqnum_t seqnum;
  omx_return_t ret;

  partner = omx__partner_from_addr(&dest_endpoint);
  seqnum = partner->next_send_seq;
  omx__debug_printf("sending %ld bytes using seqnum %d\n",
		    (unsigned long) length, seqnum);

  if (likely(length <= OMX_TINY_MAX)) {
    ret = omx__submit_or_queue_isend_tiny(ep,
					  buffer, length,
					  partner, seqnum,
					  match_info,
					  context, requestp);
  } else if (length <= OMX_SMALL_MAX) {
    ret = omx__submit_or_queue_isend_small(ep,
					   buffer, length,
					   partner, seqnum,
					   match_info,
					   context, requestp);
  } else if (length <= OMX_MEDIUM_MAX) {
    ret = omx__submit_or_queue_isend_medium(ep,
					    buffer, length,
					    partner, seqnum,
					    match_info,
					    context, requestp);
  } else {
    ret = omx__submit_or_queue_isend_large(ep,
					   buffer, length,
					   partner, seqnum,
					   match_info,
					   context, requestp);
  }

  if (likely(ret == OMX_SUCCESS)) {
    /* increase at the end, to avoid having to decrease back in case of error */
    partner->next_send_seq++;
  }

  /* progress a little bit */
  omx__progress(ep);

  return ret;
}

omx_return_t
omx_issend(struct omx_endpoint *ep,
	   void *buffer, size_t length,
	   omx_endpoint_addr_t dest_endpoint,
	   uint64_t match_info,
	   void *context, union omx_request **requestp)
{
  struct omx__partner * partner;
  omx__seqnum_t seqnum;
  omx_return_t ret;

  partner = omx__partner_from_addr(&dest_endpoint);
  seqnum = partner->next_send_seq;
  omx__debug_printf("sending %ld bytes using seqnum %d\n",
		    (unsigned long) length, seqnum);

  ret = omx__submit_or_queue_isend_large(ep,
					 buffer, length,
					 partner, seqnum,
					 match_info,
					 context, requestp);
  if (likely(ret == OMX_SUCCESS)) {
    /* increase at the end, to avoid having to decrease back in case of error */
    partner->next_send_seq++;
  }

  /* progress a little bit */
  omx__progress(ep);

  return ret;
}
