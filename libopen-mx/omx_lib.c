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
#include "omx_list.h"

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
  }
  assert(0);
}

/***************************
 * Request queue management
 */

static inline void
omx__enqueue_request(struct list_head *head,
		     union omx_request *req)
{
  list_add_tail(&req->generic.queue_elt, head);
}

static inline void
omx__dequeue_request(struct list_head *head,
		     union omx_request *req)
{
#ifdef OMX_DEBUG
  struct list_head *e;
  list_for_each(e, head)
    if (req == list_entry(e, union omx_request, generic.queue_elt))
      goto found;
  assert(0);

 found:
#endif /* OMX_DEBUG */
  list_del(&req->generic.queue_elt);
}

static inline union omx_request *
omx__queue_first_request(struct list_head *head)
{
  return list_first_entry(head, union omx_request, generic.queue_elt);
}

static inline int
omx__queue_empty(struct list_head *head)
{
  return list_empty(head);
}

/*******************
 * Receive callback
 */

typedef omx_return_t (*omx__process_recv_func_t) (struct omx_endpoint *ep,
						  union omx_evt *evt,
						  void *data);

static omx_return_t
omx__process_recv_tiny(struct omx_endpoint *ep,
		       union omx_evt *evt, void *data)
{
  struct omx_evt_recv_tiny *event = &evt->recv_tiny;
  struct omx__partner * partner;
  union omx_request *req;
  unsigned long length;
  uint16_t peer_index;
  omx_return_t ret;

  /* FIXME: use the src_peer_index with omx_connect */
  ret = omx__peer_addr_to_index(event->src_addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, event->src_addr);
    fprintf(stderr, "Failed to find peer index of board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  ret = omx__partner_lookup(ep, peer_index, event->src_endpoint,
			    &partner);
  if (ret != OMX_SUCCESS)
    return ret;

  if (omx__queue_empty(&ep->recv_req_q)) {
    void *unexp_buffer;

    req = malloc(sizeof(*req));
    if (!req) {
      fprintf(stderr, "Failed to allocate request for unexpected tiny messages, dropping\n");
      return OMX_NO_RESOURCES;
    }

    length = event->length;
    unexp_buffer = malloc(length);
    if (!unexp_buffer) {
      fprintf(stderr, "Failed to allocate buffer for unexpected tiny messages, dropping\n");
      free(req);
      return OMX_NO_RESOURCES;
    }

    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->generic.status.match_info = event->match_info;
    req->generic.status.msg_length = length;
    req->recv.buffer = unexp_buffer;

    memcpy(unexp_buffer, data, length);

    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->unexp_req_q, req);

  } else {
    req = omx__queue_first_request(&ep->recv_req_q);
    omx__dequeue_request(&ep->recv_req_q, req);

    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->generic.status.match_info = event->match_info;

    length = event->length > req->recv.length
      ? req->recv.length : event->length;
    req->generic.status.msg_length = event->length;
    req->generic.status.xfer_length = length;
    memcpy(req->recv.buffer, data, length);

    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->done_req_q, req);
  }

  return OMX_SUCCESS;
}

static omx_return_t
omx__process_recv_small(struct omx_endpoint *ep,
			union omx_evt *evt, void *data)
{
  struct omx_evt_recv_small * event = &evt->recv_small;
  struct omx__partner * partner;
  union omx_request *req;
  unsigned long length;
  uint16_t peer_index;
  omx_return_t ret;

  /* FIXME: use the src_peer_index with omx_connect */
  ret = omx__peer_addr_to_index(event->src_addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, event->src_addr);
    fprintf(stderr, "Failed to find peer index of board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  ret = omx__partner_lookup(ep, peer_index, event->src_endpoint,
			    &partner);
  if (ret != OMX_SUCCESS)
    return ret;

  if (omx__queue_empty(&ep->recv_req_q)) {
    void *unexp_buffer;

    req = malloc(sizeof(*req));
    if (!req) {
      fprintf(stderr, "Failed to allocate request for unexpected small messages, dropping\n");
      return OMX_NO_RESOURCES;
    }

    length = event->length;
    unexp_buffer = malloc(length);
    if (!unexp_buffer) {
      fprintf(stderr, "Failed to allocate buffer for unexpected small messages, dropping\n");
      free(req);
      return OMX_NO_RESOURCES;
    }

    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->generic.status.match_info = event->match_info;
    req->generic.status.msg_length = length;
    req->recv.buffer = unexp_buffer;

    memcpy(unexp_buffer, data, length);

    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->unexp_req_q, req);

  } else {
    req = omx__queue_first_request(&ep->recv_req_q);
    omx__dequeue_request(&ep->recv_req_q, req);

    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->generic.status.match_info = event->match_info;

    length = event->length > req->recv.length
      ? req->recv.length : event->length;
    req->generic.status.msg_length = event->length;
    req->generic.status.xfer_length = length;
    memcpy(req->recv.buffer, data, length);

    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->done_req_q, req);
  }

  return OMX_SUCCESS;
}

static omx_return_t
omx__process_recv_medium(struct omx_endpoint *ep,
			 union omx_evt *evt, void *data)
{
  struct omx_evt_recv_medium * event = &evt->recv_medium;
  union omx_request * req;
  unsigned long msg_length = event->msg_length;
  unsigned long chunk = event->frag_length;
  unsigned long seqnum = event->frag_seqnum;
  unsigned long offset = seqnum << (OMX_MEDIUM_FRAG_PIPELINE_BASE + event->frag_pipeline);
  struct omx__partner * partner;
  uint16_t peer_index;
  omx_return_t ret;

  printf("got a medium frag seqnum %d pipeline %d length %d offset %d of total %d\n",
	 (unsigned) seqnum, (unsigned) event->frag_pipeline, (unsigned) chunk,
	 (unsigned) offset, (unsigned) msg_length);

  /* FIXME: use the src_peer_index with omx_connect */
  ret = omx__peer_addr_to_index(event->src_addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, event->src_addr);
    fprintf(stderr, "Failed to find peer index of board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  ret = omx__partner_lookup(ep, peer_index, event->src_endpoint,
			    &partner);
  if (ret != OMX_SUCCESS)
    return ret;

  if (!omx__queue_empty(&ep->multifraq_medium_recv_req_q)) {
    /* message already partially received */

    req = omx__queue_first_request(&ep->multifraq_medium_recv_req_q);

    if (req->recv.type.medium.frags_received_mask & (1 << seqnum))
      /* already received this frag */
      return OMX_SUCCESS;

    /* take care of the data chunk */
    if (offset + chunk > msg_length)
      chunk = msg_length - offset;
    memcpy(req->recv.buffer + offset, data, chunk);
    req->recv.type.medium.frags_received_mask |= 1 << seqnum;
    req->recv.type.medium.accumulated_length += chunk;

    if (req->recv.type.medium.accumulated_length == msg_length) {
      omx__dequeue_request(&ep->multifraq_medium_recv_req_q, req);
      req->generic.state = OMX_REQUEST_STATE_DONE;
      omx__enqueue_request(&ep->done_req_q, req);
    }

    /* FIXME: do not duplicate all the code like this */

  } else if (!omx__queue_empty(&ep->recv_req_q)) {
    /* first fragment of a new message */

    req = omx__queue_first_request(&ep->recv_req_q);
    omx__dequeue_request(&ep->recv_req_q, req);

    /* set basic fields */
    omx__partner_to_addr(partner, &req->generic.status.addr);
    req->generic.status.match_info = event->match_info;

    /* compute message length */
    req->generic.status.msg_length = msg_length;
    if (msg_length > req->recv.length)
      msg_length = req->recv.length;
    req->generic.status.xfer_length = msg_length;

    /* take care of the data chunk */
    if (offset + chunk > msg_length)
      chunk = msg_length - offset;
    memcpy(req->recv.buffer + offset, data, chunk);
    req->recv.type.medium.frags_received_mask = 1 << seqnum;
    req->recv.type.medium.accumulated_length = chunk;

    if (chunk == msg_length) {
      req->generic.state = OMX_REQUEST_STATE_DONE;
      omx__enqueue_request(&ep->done_req_q, req);
    } else {
      omx__enqueue_request(&ep->multifraq_medium_recv_req_q, req);
    }

  } else {

    printf("missed a medium unexpected\n");
    /* FIXME */
  }

  return OMX_SUCCESS;
}

/*******************
 * Event processing
 */

static omx_return_t
omx__process_recv(struct omx_endpoint *ep,
		  union omx_evt *evt, omx__seqnum_t seqnum, void *data,
		  omx__process_recv_func_t recv_func)
{
  omx__debug_printf("got seqnum %d\n", seqnum);

  /* FIXME: check order, do matching, handle unexpected and early */

  return recv_func(ep, evt, data);
}

static omx_return_t
omx__process_event(struct omx_endpoint * ep, union omx_evt * evt)
{
  omx_return_t ret = OMX_SUCCESS;

  omx__debug_printf("received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case OMX_EVT_RECV_TINY: {
    ret = omx__process_recv(ep,
			    evt, evt->recv_tiny.seqnum, evt->recv_tiny.data,
			    omx__process_recv_tiny);
    break;
  }

  case OMX_EVT_RECV_SMALL: {
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index * OMX_RECVQ_ENTRY_SIZE;
    ret = omx__process_recv(ep,
			    evt, evt->recv_small.seqnum, recvq_buffer,
			    omx__process_recv_small);
    break;
  }

  case OMX_EVT_RECV_MEDIUM: {
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index * OMX_RECVQ_ENTRY_SIZE;
    ret = omx__process_recv(ep,
			    evt, evt->recv_medium.seqnum, recvq_buffer,
			    omx__process_recv_medium);
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

static omx_return_t
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

  seqnum = ep->myself->next_send_seq++; /* FIXME: increater at the end, in case of error */
  req->send.seqnum = seqnum;

  if (length <= OMX_TINY_MAX) {
    struct omx_cmd_send_tiny tiny_param;

    tiny_param.hdr.dest_addr = partner->board_addr;
    tiny_param.hdr.dest_endpoint = partner->endpoint_index;
    tiny_param.hdr.match_info = match_info;
    tiny_param.hdr.length = length;
    tiny_param.hdr.seqnum = seqnum;
    /* FIXME: tiny_param.hdr.lib_cookie = lib_cookie; */
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
    /* FIXME: small_param.lib_cookie = lib_cookie; */
    small_param.vaddr = (uintptr_t) buffer;
    small_param.seqnum = seqnum;

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
    /* FIXME: medium_param.lib_cookie = lib_cookie; */
    medium_param.msg_length = length;
    medium_param.seqnum = seqnum;

    for(i=0; i<frags; i++) {
      unsigned chunk = remaining > OMX_MEDIUM_FRAG_LENGTH_MAX
	? OMX_MEDIUM_FRAG_LENGTH_MAX : remaining;
      medium_param.frag_length = chunk;
      medium_param.frag_seqnum = i;
      medium_param.sendq_page_offset = sendq_index[i];
      printf("sending medium seqnum %d pipeline 2 length %d of total %ld\n",
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

  if (!omx__queue_empty(&ep->unexp_req_q)) {
    req = omx__queue_first_request(&ep->unexp_req_q);
    omx__dequeue_request(&ep->unexp_req_q, req);

    /* compute xfer length */
    if (length > req->generic.status.msg_length)
      length = req->generic.status.msg_length;
    req->generic.status.xfer_length = length;

    /* copy data from the unexpected buffer */
    memcpy(buffer, req->recv.buffer, length);
    free(req->recv.buffer);

    req->generic.state = OMX_REQUEST_STATE_DONE;
    omx__enqueue_request(&ep->done_req_q, req);

  } else {
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

    omx__enqueue_request(&ep->recv_req_q, req);
  }

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
