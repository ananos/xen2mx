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

#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "omx_lib.h"
#include "omx_request.h"

/******************************
 * Endpoint address management
 */

omx_return_t
omx_get_endpoint_addr(omx_endpoint_t endpoint,
		      omx_endpoint_addr_t *endpoint_addr)
{
  omx__partner_to_addr(endpoint->myself, endpoint_addr);
  return OMX_SUCCESS;
}

omx_return_t
omx_decompose_endpoint_addr(omx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);
  *nic_id = partner->board_addr;
  *endpoint_id = partner->endpoint_index;
  return OMX_SUCCESS;
}

/*********************
 * Partner management
 */

omx_return_t
omx__partner_create(struct omx_endpoint *ep, uint16_t peer_index,
		    uint64_t board_addr, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  struct omx__partner * partner;
  uint32_t partner_index;

  partner = malloc(sizeof(*partner));
  if (!partner) {
    return omx__errno_to_return(ENOMEM, "partner malloc");
  }

  partner->board_addr = board_addr;
  partner->endpoint_index = endpoint_index;
  partner->peer_index = peer_index;
  INIT_LIST_HEAD(&partner->partialq);
  partner->next_send_seq = 0;
  partner->next_match_recv_seq = 0;
  partner->next_frag_recv_seq = 0;

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__globals.endpoint_max;
  ep->partners[partner_index] = partner;

  *partnerp = partner;

  return OMX_SUCCESS;
}

omx_return_t
omx__partner_lookup(struct omx_endpoint *ep,
		    uint64_t board_addr, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  uint32_t partner_index;
  uint16_t peer_index;
  omx_return_t ret;

  ret = omx__peer_addr_to_index(board_addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, board_addr);
    fprintf(stderr, "Failed to find peer index of board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__globals.endpoint_max;

  if (!ep->partners[partner_index])
    return omx__partner_create(ep, peer_index, board_addr, endpoint_index, partnerp);


  *partnerp = ep->partners[partner_index];
  return OMX_SUCCESS;
}

/*************
 * Connection
 */

struct omx__connect_data {
  uint32_t session_id;
  uint32_t app_key;
  uint16_t seqnum_start;
  uint8_t is_reply;
  uint8_t connect_seqnum;
  uint8_t status_code;
};

/*
 * Start the connection process to another peer
 */
omx_return_t
omx_connect(omx_endpoint_t ep,
	    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	    uint32_t timeout,
	    omx_endpoint_addr_t *addr)
{
  union omx_request * req;
  struct omx__partner * partner;
  struct omx_cmd_send_connect connect_param;
  struct omx__connect_data * data = (void *) &connect_param.data;
  omx_return_t ret;
  int err;

  req = malloc(sizeof(union omx_request));
  if (!req) {
    ret = omx__errno_to_return(ENOMEM, "isend request malloc");
    goto out;
  }

  ret = omx__partner_lookup(ep, nic_id, endpoint_id, &partner);
  if (ret != OMX_SUCCESS)
    goto out_with_req;

  connect_param.hdr.dest_addr = partner->board_addr;
  connect_param.hdr.dest_endpoint = partner->endpoint_index;
  connect_param.hdr.seqnum = 0;
  connect_param.hdr.dest_peer_index = partner->peer_index;
  connect_param.hdr.length = sizeof(*data);
  data->session_id = ep->session_id;
  data->app_key = key;
  data->is_reply = 0;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT, &connect_param);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl send/connect");
    goto out_with_req;
  }
  /* no need to wait for a done event, connect is synchronous */

  req->generic.type = OMX_REQUEST_TYPE_CONNECT;
  req->generic.state = OMX_REQUEST_STATE_PENDING;
  req->connect.partner = partner;
  req->connect.session_id = ep->session_id;
  omx__enqueue_request(&ep->connect_req_q, req);

  printf("waiting for connect reply\n");
  while (req->generic.state == OMX_REQUEST_STATE_PENDING) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out_with_req_queued;
  }
  omx__dequeue_request(&ep->connect_req_q, req);
  printf("connect done\n");

  switch (req->generic.status.code) {
  case OMX_STATUS_SUCCESS:
    omx__partner_to_addr(partner, addr);
    ret = OMX_SUCCESS;
    break;
  case OMX_STATUS_BAD_KEY:
    ret = OMX_BAD_CONNECTION_KEY;
    break;
  default:
    assert(0);
  }

  return ret;

 out_with_req_queued:
  omx__dequeue_request(&ep->connect_req_q, req);
 out_with_req:
  free(req);
 out:
  return ret;
}

/*
 * End the connection process to another peer
 */
static inline omx_return_t
omx__process_recv_connect_reply(struct omx_endpoint *ep,
				struct omx_evt_recv_connect *event)
{
  struct omx__partner * partner;
  struct omx__connect_data * reply_data = (void *) event->data;
  union omx_request * req;
  omx_return_t ret;

  ret = omx__partner_lookup(ep, event->src_addr, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_INVALID_PARAMETER)
      fprintf(stderr, "Open-MX: Received connect from unknown peer\n");
    return ret;
  }

  omx__foreach_request(&ep->connect_req_q, req)
    if (reply_data->session_id == ep->session_id
	&& partner == req->connect.partner) {
      /* FIXME check connect seqnum */
      goto found;
  }

  /* invalid connect reply, just ignore it */
  return OMX_SUCCESS;

 found:
  printf("waking up on connect reply\n");
  req->generic.status.code = reply_data->status_code;
  req->generic.state = OMX_REQUEST_STATE_DONE;

  return OMX_SUCCESS;
}

/*
 * Another peer is connecting to us
 */
static inline omx_return_t
omx__process_recv_connect_request(struct omx_endpoint *ep,
				  struct omx_evt_recv_connect *event)
{
  struct omx__partner * partner;
  struct omx_cmd_send_connect reply_param;
  struct omx__connect_data * request_data = (void *) event->data;
  struct omx__connect_data * reply_data = (void *) reply_param.data;
  omx_return_t ret;
  omx_status_code_t status_code;
  int err;

  ret = omx__partner_lookup(ep, event->src_addr, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_INVALID_PARAMETER)
      fprintf(stderr, "Open-MX: Received connect from unknown peer\n");
    return ret;
  }

  if (request_data->app_key == ep->app_key) {
    /* FIXME: do the connection stuff:
     * + initialize receive seqnums
     * + save the session
     */

    status_code = OMX_STATUS_SUCCESS;
  } else {
    status_code = OMX_STATUS_BAD_KEY;
  }

  printf("got a connect, replying\n");

  reply_param.hdr.dest_addr = partner->board_addr;
  reply_param.hdr.dest_endpoint = partner->endpoint_index;
  reply_param.hdr.seqnum = 0;
  reply_param.hdr.dest_peer_index = partner->peer_index;
  reply_param.hdr.length = sizeof(*reply_data);
  reply_data->is_reply = 1;
  reply_data->session_id = request_data->session_id;
  reply_data->status_code = status_code;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT, &reply_param);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl send/connect");
    goto out_with_req;
  }
  /* no need to wait for a done event, connect is synchronous */

  return OMX_SUCCESS;

 out_with_req:
  return ret;
}

/*
 * Incoming connection message
 */
omx_return_t
omx__process_recv_connect(struct omx_endpoint *ep,
			  struct omx_evt_recv_connect *event)
{
  struct omx__connect_data * data = (void *) event->data;
  if (data->is_reply)
    return omx__process_recv_connect_reply(ep, event);
  else
    return omx__process_recv_connect_request(ep, event);
}
