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
  if (!partner)
    return OMX_NO_RESOURCES;

  partner->board_addr = board_addr;
  partner->endpoint_index = endpoint_index;
  partner->peer_index = peer_index;
  partner->dest_src_peer_index = -1;
  partner->connect_seqnum = 0;
  INIT_LIST_HEAD(&partner->partialq);
  partner->session_id = 0; /* will be initialized when the partner will connect to me */
  partner->next_send_seq = -1; /* will be initialized when the partner will reply to my connect */
  partner->next_match_recv_seq = 0;
  partner->next_frag_recv_seq = 0;

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__globals.endpoint_max;
  ep->partners[partner_index] = partner;

  *partnerp = partner;
  omx__debug_printf("created peer %d %d\n", peer_index, endpoint_index);

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

omx_return_t
omx__partner_recv_lookup(struct omx_endpoint *ep,
			 uint16_t peer_index, uint8_t endpoint_index,
			 struct omx__partner ** partnerp)
{
  uint32_t partner_index;
  struct omx__partner * partner;

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__globals.endpoint_max;
  partner = ep->partners[partner_index];
  assert(partner);

  *partnerp = partner;
  return OMX_SUCCESS;
}

/*
 * Actually initialize connected partner
 */
static inline void
omx__connect_partner(struct omx__partner * partner,
		     uint16_t src_dest_peer_index,
		     uint32_t target_session_id,
		     omx__seqnum_t target_recv_seqnum_start)
{
    partner->dest_src_peer_index = src_dest_peer_index;
    partner->session_id = target_session_id;
    partner->next_send_seq = target_recv_seqnum_start;
}

omx_return_t
omx__connect_myself(struct omx_endpoint *ep, uint64_t board_addr)
{
  uint16_t peer_index;
  omx_return_t ret;

  ret = omx__peer_addr_to_index(board_addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, board_addr);
    fprintf(stderr, "Failed to find peer index of local board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  ret = omx__partner_create(ep, peer_index,
			    board_addr, ep->endpoint_index,
			    &ep->myself);
  if (ret != OMX_SUCCESS)
    return ret;

  omx__connect_partner(ep->myself, peer_index, ep->session_id, 0);

  return OMX_SUCCESS;
}

/*************
 * Connection
 */

struct omx__connect_request_data {
  /* the sender's session id (so that we know when the connect has been sent) */
  uint32_t src_session_id;
  /* the application level key in the request */
  uint32_t app_key;

  uint16_t pad1;
  /* is this a request ot a reply? 0 here */
  uint8_t is_reply;
  /* sequence number of this connect request (in case multiple have been sent/lost) */
  uint8_t connect_seqnum;

  uint8_t pad2;
};

struct omx__connect_reply_data {
  /* the sender's session id (so that we know when the connect has been sent) */
  uint32_t src_session_id;
  /* the target session_id (so that we can send right after this connect) */
  uint32_t target_session_id;
  /* the target next recv seqnum in the reply (so that we know out next send seqnum) */
  uint16_t target_recv_seqnum_start;
  /* is this a request ot a reply? 1 here */
  uint8_t is_reply;
  /* sequence number of this connect request (in case multiple have been sent/lost) */
  uint8_t connect_seqnum;
  /* the target connect matching status (only in the reply) */
  uint8_t status_code;
};

/* FIXME: assertions so that is_reply is at the same offset/size */

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
  struct omx__connect_request_data * data = (void *) &connect_param.data;
  uint8_t connect_seqnum;
  omx_return_t ret;
  int err;

  req = omx__request_alloc(OMX_REQUEST_TYPE_CONNECT);
  if (!req) {
    ret = OMX_NO_RESOURCES;
    goto out;
  }

  ret = omx__partner_lookup(ep, nic_id, endpoint_id, &partner);
  if (ret != OMX_SUCCESS)
    goto out_with_req;

  connect_seqnum = partner->connect_seqnum++;

  connect_param.hdr.dest_addr = partner->board_addr;
  connect_param.hdr.dest_endpoint = partner->endpoint_index;
  connect_param.hdr.seqnum = 0;
  connect_param.hdr.src_dest_peer_index = partner->peer_index;
  connect_param.hdr.length = sizeof(*data);
  data->src_session_id = ep->session_id;
  data->app_key = key;
  data->connect_seqnum = connect_seqnum;
  data->is_reply = 0;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT, &connect_param);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl SEND_CONNECT");
    goto out_with_req;
  }
  /* no need to wait for a done event, connect is synchronous */

  req->generic.state = OMX_REQUEST_STATE_PENDING;
  req->connect.partner = partner;
  req->connect.session_id = ep->session_id;
  req->connect.connect_seqnum = connect_seqnum;
  omx__enqueue_request(&ep->connect_req_q, req);

  omx__debug_printf("waiting for connect reply\n");
  while (req->generic.state == OMX_REQUEST_STATE_PENDING) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out_with_req_queued;
  }
  omx__dequeue_request(&ep->connect_req_q, req);
  omx__debug_printf("connect done\n");

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
  omx__request_free(req);
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
  struct omx__connect_reply_data * reply_data = (void *) event->data;
  union omx_request * req;
  omx_return_t ret;

  ret = omx__partner_lookup(ep, event->src_addr, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_INVALID_PARAMETER)
      fprintf(stderr, "Open-MX: Received connect from unknown peer\n");
    return ret;
  }

  omx__foreach_request(&ep->connect_req_q, req) {
    /* check the endpoint session (so that the endpoint didn't close/reopen in the meantime)
     * and the partner and the connection seqnum given by this partner
     */
    if (reply_data->src_session_id == ep->session_id
	&& partner == req->connect.partner
	&& reply_data->connect_seqnum == req->connect.connect_seqnum) {
      goto found;
    }
  }

  /* invalid connect reply, just ignore it */
  return OMX_SUCCESS;

 found:
  omx__debug_printf("waking up on connect reply\n");

  if (reply_data->status_code == OMX_STATUS_SUCCESS) {
    /* connection successfull, initialize stuff */
    omx__connect_partner(partner,
			 event->src_dest_peer_index,
			 reply_data->target_session_id,
			 reply_data->target_recv_seqnum_start);
  }

  /* complete the request */
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
  struct omx__connect_request_data * request_data = (void *) event->data;
  struct omx__connect_reply_data * reply_data = (void *) reply_param.data;
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
    /* FIXME: do bidirectionnal connection stuff? */

    status_code = OMX_STATUS_SUCCESS;
  } else {
    status_code = OMX_STATUS_BAD_KEY;
  }

  omx__debug_printf("got a connect, replying\n");

  reply_param.hdr.dest_addr = partner->board_addr;
  reply_param.hdr.dest_endpoint = partner->endpoint_index;
  reply_param.hdr.seqnum = 0;
  reply_param.hdr.src_dest_peer_index = partner->peer_index;
  reply_param.hdr.length = sizeof(*reply_data);
  reply_data->is_reply = 1;
  reply_data->target_session_id = ep->session_id;
  reply_data->src_session_id = request_data->src_session_id;
  reply_data->connect_seqnum = request_data->connect_seqnum;
  reply_data->status_code = status_code;
  reply_data->target_recv_seqnum_start = 0;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT, &reply_param);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl SEND_CONNECT reply");
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
  struct omx__connect_request_data * data = (void *) event->data;
  if (data->is_reply)
    return omx__process_recv_connect_reply(ep, event);
  else
    return omx__process_recv_connect_request(ep, event);
}
