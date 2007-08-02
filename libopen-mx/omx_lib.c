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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>

#include "omx_io.h"
#include "omx_lib.h"
#include "omx_request.h"

/***********************
 * Management of errors
 */

omx_return_t
omx__errno_to_return(int error, char * caller)
{
  switch (error) {
  case EINVAL:
    return OMX_INVALID_PARAMETER;
  case EACCES:
  case EPERM:
    return OMX_ACCESS_DENIED;
  case EMFILE:
  case ENFILE:
  case ENOMEM:
    return OMX_NO_SYSTEM_RESOURCES;
  case ENODEV:
  case ENOENT:
    return OMX_NO_DEVICE;
  default:
    fprintf(stderr, "Open-MX: %s got unexpected errno %d (%s)\n",
	    caller, error, strerror(error));
    return OMX_BAD_ERROR;
  }
}

const char *
omx_strerror(omx_return_t ret)
{
  switch (ret) {
  case OMX_SUCCESS:
    return "Success";
  case OMX_BAD_ERROR:
    return "Bad (internal?) error";
  case OMX_ALREADY_INITIALIZED:
    return "Already initialized";
  case OMX_NOT_INITIALIZED:
    return "Not initialized";
  case OMX_NO_DEVICE:
    return "No device";
  case OMX_ACCESS_DENIED:
    return "Access denied";
  case OMX_NO_RESOURCES:
    return "No resources available";
  case OMX_NO_SYSTEM_RESOURCES:
    return "No resources available in the system";
  case OMX_INVALID_PARAMETER:
    return "Invalid parameter";
  case OMX_NOT_IMPLEMENTED:
    return "Not implemented";
  case OMX_BAD_CONNECTION_KEY:
    return "Bad Connection Key";
  }
  assert(0);
}

const char *
omx_strstatus(omx_status_code_t code)
{
  switch (code) {
  case OMX_STATUS_SUCCESS:
    return "Success";
  case OMX_STATUS_FAILED:
    return "Failed";
  case OMX_STATUS_BAD_KEY:
    return "Bad Connection Key";
  }
  assert(0);
}

/*******************
 * Receive callback
 */

typedef void (*omx__process_recv_func_t) (struct omx_endpoint *ep,
					  struct omx__partner *partner,
					  union omx_request *req,
					  union omx_evt *evt,
					  void *data, uint32_t length);

static void
omx__process_recv_tiny(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       union omx_evt *evt,
		       void *data, uint32_t length)
{
  memcpy(req->recv.buffer, data, length);

  req->generic.state = OMX_REQUEST_STATE_DONE;
  req->generic.status.code = OMX_STATUS_SUCCESS;
  omx__enqueue_request(req->recv.unexpected ? &ep->unexp_req_q : &ep->done_req_q,
		       req);
}

static void
omx__process_recv_small(struct omx_endpoint *ep, struct omx__partner *partner,
			union omx_request *req,
			union omx_evt *evt,
			void *data, uint32_t length)
{
  memcpy(req->recv.buffer, data, length);

  req->generic.state = OMX_REQUEST_STATE_DONE;
  req->generic.status.code = OMX_STATUS_SUCCESS;
  omx__enqueue_request(req->recv.unexpected ? &ep->unexp_req_q : &ep->done_req_q,
		       req);
}

static void
omx__process_recv_medium_frag(struct omx_endpoint *ep, struct omx__partner *partner,
			      union omx_request *req,
			      union omx_evt *evt,
			      void *data, uint32_t msg_length)
{
  struct omx_evt_recv_msg * event = &evt->recv_msg;
  unsigned long chunk = event->specific.medium.frag_length;
  unsigned long frag_seqnum = event->specific.medium.frag_seqnum;
  unsigned long frag_pipeline = event->specific.medium.frag_pipeline;
  unsigned long offset = frag_seqnum << (OMX_MEDIUM_FRAG_PIPELINE_BASE + frag_pipeline);
  int new = (req->recv.type.medium.frags_received_mask == 0);

  omx__debug_printf("got a medium frag seqnum %d pipeline %d length %d offset %d of total %d\n",
		    (unsigned) frag_seqnum, (unsigned) frag_pipeline, (unsigned) chunk,
		    (unsigned) offset, (unsigned) msg_length);

  if (req->recv.type.medium.frags_received_mask & (1 << frag_seqnum))
    /* already received this frag */
    return;

  /* take care of the data chunk */
  if (offset + chunk > msg_length)
    chunk = msg_length - offset;
  memcpy(req->recv.buffer + offset, data, chunk);
  req->recv.type.medium.frags_received_mask |= 1 << frag_seqnum;
  req->recv.type.medium.accumulated_length += chunk;

  if (req->recv.type.medium.accumulated_length == msg_length) {
    /* was the last frag */
    omx__debug_printf("got last frag of seqnum %d\n", req->recv.seqnum);

    /* if there were previous frags, remove from the partialq */
    if (!new)
      omx__dequeue_partner_request(partner, req);

    req->generic.state = OMX_REQUEST_STATE_DONE;
    req->generic.status.code = OMX_STATUS_SUCCESS;
    omx__enqueue_request(req->recv.unexpected ? &ep->unexp_req_q : &ep->done_req_q,
			 req);

  } else {
    /* more frags missing */
    omx__debug_printf("got one frag of seqnum %d\n", req->recv.seqnum);

    if (new)
      omx__enqueue_partner_request(partner, req);

    omx__enqueue_request(req->recv.unexpected ? &ep->unexp_req_q : &ep->multifrag_medium_recv_req_q,
			 req);
  }
}

/*******************
 * Event processing
 */

static inline omx_return_t
omx__match_recv(struct omx_endpoint *ep,
		union omx_evt *evt,
		union omx_request **reqp)
{
  uint64_t match_info = evt->recv_msg.match_info;
  union omx_request * req;

  omx__foreach_request(&ep->recv_req_q, req)
    if (req->recv.match_info == (req->recv.match_mask & match_info)) {
      /* matched a posted recv */
      omx__dequeue_request(&ep->recv_req_q, req);
      *reqp = req;
      return OMX_SUCCESS;
    }

  return OMX_SUCCESS;
}

static inline omx_return_t
omx__try_match_next_recv(struct omx_endpoint *ep,
			 struct omx__partner * partner, omx__seqnum_t seqnum,
			 union omx_evt *evt, void *data, uint32_t length,
			 omx__process_recv_func_t recv_func)
{
  union omx_request * req = NULL;
  omx_return_t ret;

  /* try to match */
  ret = omx__match_recv(ep, evt, &req);
  if (ret != OMX_SUCCESS)
    return ret;

  if (req) {
    /* expected */
    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->recv.seqnum = seqnum;
    req->generic.status.match_info = evt->recv_msg.match_info;
    req->generic.type = OMX_REQUEST_TYPE_RECV;
    req->generic.state = OMX_REQUEST_STATE_PENDING;
    req->recv.unexpected = 0;
    memset(&req->recv.type, 0, sizeof(req->recv.type));

    req->generic.status.msg_length = length;
    if (req->recv.length < length)
      length = req->recv.length;
    req->generic.status.xfer_length = length;

    (*recv_func)(ep, partner, req, evt, data, length);

  } else {
    /* unexpected */
    void *unexp_buffer;

    req = malloc(sizeof(*req));
    if (!req) {
      fprintf(stderr, "Failed to allocate request for unexpected messages, dropping\n");
      return OMX_NO_RESOURCES;
    }

    unexp_buffer = malloc(length);
    if (!unexp_buffer) {
      fprintf(stderr, "Failed to allocate buffer for unexpected messages, dropping\n");
      free(req);
      return OMX_NO_RESOURCES;
    }

    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->recv.seqnum = seqnum;
    req->generic.status.match_info = evt->recv_msg.match_info;
    req->generic.type = OMX_REQUEST_TYPE_RECV;
    req->generic.state = OMX_REQUEST_STATE_PENDING;
    req->recv.unexpected = 1;
    memset(&req->recv.type, 0, sizeof(req->recv.type));

    req->generic.status.msg_length = length;
    req->recv.buffer = unexp_buffer;

    (*recv_func)(ep, partner, req, evt, data, length);

  }

  /* we match this seqnum, we now expect the next one */
  partner->next_match_recv_seq++;

  /* if no more partner partial request, we expect a frag for the new seqnum,
   * if not, we expect the fragment for at least the first partial seqnum
   */
  if (omx__partner_queue_empty(partner)) {
    partner->next_frag_recv_seq = partner->next_match_recv_seq;
  } else {
    union omx_request *req = omx__partner_queue_first_request(partner);
    partner->next_frag_recv_seq = req->recv.seqnum;
  }

  return OMX_SUCCESS;
}

static inline omx_return_t
omx__continue_partial_request(struct omx_endpoint *ep,
			      struct omx__partner * partner, omx__seqnum_t seqnum,
			      union omx_evt *evt, void *data, uint32_t msg_length)
{
  union omx_request * req = NULL;

  omx__foreach_partner_request(partner, req) {
    if (req->recv.seqnum == seqnum) {
      omx__dequeue_request(req->recv.unexpected ? &ep->unexp_req_q : &ep->multifrag_medium_recv_req_q,
			   req);
      omx__process_recv_medium_frag(ep, partner, req,
				    evt, data, msg_length);

      /* if no more partner partial request, we expect a frag for the new seqnum,
       * if not, we expect the fragment for at least the first partial seqnum
       */
      if (omx__partner_queue_empty(partner)) {
	partner->next_frag_recv_seq = partner->next_match_recv_seq;
      } else {
	union omx_request *req = omx__partner_queue_first_request(partner);
	partner->next_frag_recv_seq = req->recv.seqnum;
      }

      return OMX_SUCCESS;
    }
  }

  assert(0);
}

static omx_return_t
omx__process_recv(struct omx_endpoint *ep,
		  union omx_evt *evt, void *data, uint32_t length,
		  omx__process_recv_func_t recv_func)
{
  struct omx_evt_recv_msg * msg = &evt->recv_msg;
  omx__seqnum_t seqnum = msg->seqnum;
  struct omx__partner * partner;
  omx_return_t ret;

  ret = omx__partner_recv_lookup(ep, msg->dest_src_peer_index, msg->src_endpoint,
				 &partner);
  if (ret != OMX_SUCCESS)
    return ret;

  omx__debug_printf("got seqnum %d, expected match at %d, frag at %d\n",
		    seqnum, partner->next_match_recv_seq, partner->next_frag_recv_seq);

  if (seqnum == partner->next_match_recv_seq) {
    /* expected seqnum, do the matching */
    ret = omx__try_match_next_recv(ep, partner, seqnum,
				   evt, data, length,
				   recv_func);

  } else if (seqnum > partner->next_frag_recv_seq) {
    /* early fragment or message */
    assert(0); /* FIXME */

  } else if (msg->type == OMX_EVT_RECV_MEDIUM
	     && seqnum >= partner->next_frag_recv_seq) {
    /* fragment of already matched but incomplete medium message */
    ret = omx__continue_partial_request(ep, partner, seqnum,
					evt, data, length);

  } else {
    /* obsolete fragment or message, ignore it */
  }

  return ret;
}

static omx_return_t
omx__process_event(struct omx_endpoint * ep, union omx_evt * evt)
{
  omx_return_t ret = OMX_SUCCESS;

  omx__debug_printf("received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case OMX_EVT_RECV_CONNECT: {
    ret = omx__process_recv_connect(ep, &evt->recv_connect);
    break;
  }

  case OMX_EVT_RECV_TINY: {
    ret = omx__process_recv(ep,
			    evt, evt->recv_msg.specific.tiny.data, evt->recv_msg.specific.tiny.length,
			    omx__process_recv_tiny);
    break;
  }

  case OMX_EVT_RECV_SMALL: {
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index * OMX_RECVQ_ENTRY_SIZE;
    ret = omx__process_recv(ep,
			    evt, recvq_buffer, evt->recv_msg.specific.small.length,
			    omx__process_recv_small);
    break;
  }

  case OMX_EVT_RECV_MEDIUM: {
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index * OMX_RECVQ_ENTRY_SIZE;
    ret = omx__process_recv(ep,
			    evt, recvq_buffer, evt->recv_msg.specific.medium.msg_length,
			    omx__process_recv_medium_frag);
    break;
  }

  case OMX_EVT_SEND_MEDIUM_FRAG_DONE: {
    uint16_t sendq_page_offset = evt->send_medium_frag_done.sendq_page_offset;
    union omx_request * req = omx__endpoint_sendq_map_put(ep, sendq_page_offset);
    assert(req
	   && req->generic.type == OMX_REQUEST_TYPE_SEND_MEDIUM);

    /* message is not done */
    if (--req->send.type.medium.frags_pending_nr)
      break;

    omx__dequeue_request(&ep->sent_req_q, req);
    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->done_req_q, req);
    break;
  }

  default:
    printf("unknown type\n");
    assert(0);
  }

  return ret;
}

/*******************
 * Main progression
 */

omx_return_t
omx__progress(struct omx_endpoint * ep)
{
  /* process events */
  while (1) {
    volatile union omx_evt * evt = ep->next_event;

    if (evt->generic.type == OMX_EVT_NONE)
      break;

    omx__process_event(ep, (union omx_evt *) evt);

    /* mark event as done */
    evt->generic.type = OMX_EVT_NONE;

    /* next event */
    evt++;
    if ((void *) evt >= ep->eventq + OMX_EVENTQ_SIZE)
      evt = ep->eventq;
    ep->next_event = (void *) evt;
  }

  return OMX_SUCCESS;
}

/**************************
 * Main send/recv routines
 */

omx_return_t
omx_isend(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  uint64_t match_info,
	  omx_endpoint_addr_t dest_endpoint,
	  void *context, union omx_request **requestp)
{
  union omx_request * req;
  struct omx__partner * partner;
  omx__seqnum_t seqnum;
  omx_return_t ret;
  int err;

  req = malloc(sizeof(union omx_request));
  if (!req) {
    ret = omx__errno_to_return(ENOMEM, "isend request malloc");
    goto out;
  }

  partner = omx__partner_from_addr(&dest_endpoint);
  req->send.partner = partner;
  omx__partner_to_addr(partner, &req->generic.status.addr);

  seqnum = partner->next_send_seq++; /* FIXME: increase at the end, in case of error */
  req->send.seqnum = seqnum;

  if (length <= OMX_TINY_MAX) {
    struct omx_cmd_send_tiny tiny_param;

    tiny_param.hdr.dest_addr = partner->board_addr;
    tiny_param.hdr.dest_endpoint = partner->endpoint_index;
    tiny_param.hdr.match_info = match_info;
    tiny_param.hdr.length = length;
    tiny_param.hdr.seqnum = seqnum;
    tiny_param.hdr.session_id = partner->session_id;
    tiny_param.hdr.dest_src_peer_index = partner->dest_src_peer_index;

    memcpy(tiny_param.data, buffer, length);

    err = ioctl(ep->fd, OMX_CMD_SEND_TINY, &tiny_param);
    if (err < 0) {
      ret = omx__errno_to_return(errno, "ioctl send/tiny");
      goto out_with_req;
    }
    /* no need to wait for a done event, tiny is synchronous */

    req->generic.type = OMX_REQUEST_TYPE_SEND_TINY;
    req->generic.status.context = context;
    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->done_req_q, req);

  } else if (length <= OMX_SMALL_MAX) {
    struct omx_cmd_send_small small_param;

    small_param.dest_addr = partner->board_addr;
    small_param.dest_endpoint = partner->endpoint_index;
    small_param.match_info = match_info;
    small_param.length = length;
    small_param.vaddr = (uintptr_t) buffer;
    small_param.seqnum = seqnum;
    small_param.session_id = partner->session_id;
    small_param.dest_src_peer_index = partner->dest_src_peer_index;

    err = ioctl(ep->fd, OMX_CMD_SEND_SMALL, &small_param);
    if (err < 0) {
      ret = omx__errno_to_return(errno, "ioctl send/small");
      goto out_with_req;
    }
    /* no need to wait for a done event, small is synchronous */

    req->generic.type = OMX_REQUEST_TYPE_SEND_SMALL;
    req->generic.status.context = context;
    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->done_req_q, req);

  } else {
    struct omx_cmd_send_medium medium_param;
    uint32_t remaining = length;
    uint32_t offset = 0;
    int sendq_index[8];
    int frags;
    int i;

    frags = OMX_MEDIUM_FRAGS_NR(length);
    omx__debug_assert(frags <= 8); /* for the sendq_index array above */

    if (omx__endpoint_sendq_map_get(ep, frags, req, sendq_index) < 0)
      /* FIXME: queue */
      assert(0);

    medium_param.dest_addr = partner->board_addr;
    medium_param.dest_endpoint = partner->endpoint_index;
    medium_param.match_info = match_info;
    medium_param.frag_pipeline = OMX_MEDIUM_FRAG_PIPELINE;
    medium_param.msg_length = length;
    medium_param.seqnum = seqnum;
    medium_param.session_id = partner->session_id;
    medium_param.dest_src_peer_index = partner->dest_src_peer_index;

    for(i=0; i<frags; i++) {
      unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
	? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
      medium_param.frag_length = chunk;
      medium_param.frag_seqnum = i;
      medium_param.sendq_page_offset = sendq_index[i];
      omx__debug_printf("sending medium seqnum %d pipeline 2 length %d of total %ld\n",
			i, chunk, (unsigned long) length);
      memcpy(ep->sendq + (sendq_index[i] << OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT), buffer + offset, length);

      err = ioctl(ep->fd, OMX_CMD_SEND_MEDIUM, &medium_param);
      if (err < 0) {
	ret = omx__errno_to_return(errno, "ioctl send/medium");
	goto out_with_req;
      }

      remaining -= chunk;
      offset += chunk;
    }

    /* need to wait for a done event, since the sendq pages
     * might still be in use
     */
    req->send.type.medium.frags_pending_nr = frags;
    req->generic.type = OMX_REQUEST_TYPE_SEND_MEDIUM;
    req->generic.status.context = context;
    req->generic.state = OMX_REQUEST_STATE_PENDING;
    omx__enqueue_request(&ep->sent_req_q, req);
  }

  omx__progress(ep);

  *requestp = req;

  return OMX_SUCCESS;

 out_with_req:
  free(req);
 out:
  return ret;
}

omx_return_t
omx_irecv(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, union omx_request **requestp)
{
  union omx_request * req;
  omx_return_t ret;

  omx__foreach_request(&ep->unexp_req_q, req)
    if ((req->generic.status.match_info & match_mask) == match_info) {
      /* matched an unexpected */
      omx__dequeue_request(&ep->unexp_req_q, req);

      /* compute xfer length */
      if (length > req->generic.status.msg_length)
	length = req->generic.status.msg_length;
      req->generic.status.xfer_length = length;

      /* copy data from the unexpected buffer */
      memcpy(buffer, req->recv.buffer, length);
      free(req->recv.buffer);

      omx__debug_assert(req->recv.unexpected);
      req->recv.unexpected = 0;

      if (req->generic.state == OMX_REQUEST_STATE_DONE) {
	omx__enqueue_request(&ep->done_req_q, req);
      } else {
	omx__enqueue_request(&ep->multifrag_medium_recv_req_q, req);
      }

      *requestp = req;

      return OMX_SUCCESS;
    }

  /* allocate a new recv request */
  req = malloc(sizeof(union omx_request));
  if (!req) {
    ret = omx__errno_to_return(ENOMEM, "irecv request malloc");
    goto out;
  }

  req->generic.type = OMX_REQUEST_TYPE_RECV;
  req->generic.state = OMX_REQUEST_STATE_PENDING;
  req->generic.status.context = context;
  req->recv.buffer = buffer;
  req->recv.length = length;
  req->recv.match_info = match_info;
  req->recv.match_mask = match_mask;

  omx__enqueue_request(&ep->recv_req_q, req);
  omx__progress(ep);

  *requestp = req;

  return OMX_SUCCESS;

 out:
  return ret;
}

/********************************
 * Main completion test routines
 */

omx_return_t
omx_test(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result)
{
  union omx_request * req = *requestp;
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out;

  if (req->generic.state != OMX_REQUEST_STATE_DONE) {
    *result = 0;
  } else {
    omx__dequeue_request(&ep->done_req_q, req);
    memcpy(status, &req->generic.status, sizeof(*status));

    free(req);
    *requestp = NULL;
    *result = 1;
  }

 out:
  return ret;
}

omx_return_t
omx_wait(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result)
{
  union omx_request * req = *requestp;
  omx_return_t ret = OMX_SUCCESS;

  while (req->generic.state != OMX_REQUEST_STATE_DONE) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out;
    /* FIXME: sleep */
  }

  omx__dequeue_request(&ep->done_req_q, req);
  memcpy(status, &req->generic.status, sizeof(*status));

  free(req);
  *requestp = NULL;
  *result = 1;

 out:
  return ret;
}

omx_return_t
omx_ipeek(struct omx_endpoint *ep, union omx_request **requestp,
	  uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out;

  if (omx__queue_empty(&ep->done_req_q)) {
    *result = 0;
  } else {
    *requestp = omx__queue_first_request(&ep->done_req_q);
    *result = 1;
  }

 out:
  return ret;
}

omx_return_t
omx_peek(struct omx_endpoint *ep, union omx_request **requestp,
	 uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  while (omx__queue_empty(&ep->done_req_q)) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out;
    /* FIXME: sleep */
  }

  *requestp = omx__queue_first_request(&ep->done_req_q);
  *result = 1;

 out:
  return ret;
}

/* FIXME: test/wait_any */
