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
#include "omx_wire_access.h"
#include "omx_lib_wire.h"

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

void
omx__partner_reset(struct omx__partner *partner)
{
  INIT_LIST_HEAD(&partner->non_acked_req_q);
  INIT_LIST_HEAD(&partner->pending_connect_req_q);
  INIT_LIST_HEAD(&partner->partial_recv_req_q);
  INIT_LIST_HEAD(&partner->early_recv_q);

  partner->true_session_id = -1; /* will be initialized when we will be connected to the peer */
  partner->back_session_id = -1; /* will be initialized when the partner will connect to me */
  partner->next_send_seq = -1; /* will be initialized when the partner will reply to my connect */
  partner->next_acked_send_seq = -1; /* will be initialized when the partner will reply to my connect */
  partner->next_match_recv_seq = OMX__SEQNUM(1); /* will force the sender's send seq through the connect */
  partner->next_frag_recv_seq = OMX__SEQNUM(1); /* will force the sender's send seq through the connect */
  partner->connect_seqnum = 0;
  partner->last_send_acknum = 0;
  partner->last_recv_acknum = 0;

  /* FIXME: remove the endpoint_partners_to_ack_elt if necessary */

  partner->oldest_recv_time_not_acked = 0;
}

omx_return_t
omx__partner_create(struct omx_endpoint *ep, uint16_t peer_index,
		    uint64_t board_addr, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  struct omx__partner * partner;
  uint32_t partner_index;

  partner = malloc(sizeof(*partner));
  if (unlikely(!partner))
    return OMX_NO_RESOURCES;

  partner->board_addr = board_addr;
  partner->endpoint_index = endpoint_index;
  partner->peer_index = peer_index;
  partner->localization = OMX__PARTNER_LOCALIZATION_UNKNOWN; /* will be set by omx__partner_check_localization() */

  omx__partner_reset(partner);

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__driver_desc->endpoint_max;
  ep->partners[partner_index] = partner;

  *partnerp = partner;
  omx__debug_printf(CONNECT, "created peer %d %d\n", peer_index, endpoint_index);

  return OMX_SUCCESS;
}

static INLINE void
omx__partner_check_localization(struct omx__partner * partner, int shared)
{
  enum omx__partner_localization localization;

#ifdef OMX_DISABLE_SHARED
  localization = OMX__PARTNER_LOCALIZATION_REMOTE;
#else
  localization = shared ? OMX__PARTNER_LOCALIZATION_LOCAL : OMX__PARTNER_LOCALIZATION_REMOTE;
#endif

  if (partner->localization == OMX__PARTNER_LOCALIZATION_UNKNOWN) {
    partner->localization = localization;
    omx__debug_printf(MAIN, "using shared communication for partner index %d\n", (unsigned) partner->peer_index);
  } else {
    omx__debug_assert(partner->localization == localization);
  }
}

omx_return_t
omx__partner_lookup(struct omx_endpoint *ep,
		    uint16_t peer_index, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  uint32_t partner_index;

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__driver_desc->endpoint_max;

  if (unlikely(!ep->partners[partner_index])) {
    uint64_t board_addr;
    omx_return_t ret;

    ret = omx__peer_index_to_addr(peer_index, &board_addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to find peer address of index %d (%s)\n",
	      (unsigned) peer_index, omx_strerror(ret));
      return ret;
    }

    return omx__partner_create(ep, peer_index, board_addr, endpoint_index, partnerp);
  }

  *partnerp = ep->partners[partner_index];
  return OMX_SUCCESS;
}

omx_return_t
omx__partner_lookup_by_addr(struct omx_endpoint *ep,
			    uint64_t board_addr, uint8_t endpoint_index,
			    struct omx__partner ** partnerp)
{
  uint32_t partner_index;
  uint16_t peer_index;
  omx_return_t ret;

  ret = omx__peer_addr_to_index(board_addr, &peer_index);
  if (unlikely(ret != OMX_SUCCESS)) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, board_addr);
    fprintf(stderr, "Failed to find peer index of board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__driver_desc->endpoint_max;

  if (unlikely(!ep->partners[partner_index]))
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
    + ((uint32_t) peer_index) * omx__driver_desc->endpoint_max;
  partner = ep->partners[partner_index];
  omx__debug_assert(partner);

  *partnerp = partner;
  return OMX_SUCCESS;
}

omx_return_t
omx__connect_myself(struct omx_endpoint *ep, uint64_t board_addr)
{
  uint16_t peer_index;
  omx_return_t ret;
  int maybe_self = 0;
  int maybe_shared = 0;

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

  ep->myself->next_send_seq = OMX__SEQNUM(1);
  ep->myself->next_acked_send_seq = OMX__SEQNUM(1);
  ep->myself->true_session_id = ep->desc->session_id;
  ep->myself->back_session_id = ep->desc->session_id;

#ifndef OMX_DISABLE_SELF
  maybe_self = omx__globals.selfcomms;
#endif
#ifndef OMX_DISABLE_SHARED
  maybe_shared = omx__globals.sharedcomms;
#endif
  ep->myself->localization = (maybe_self || maybe_shared) ? OMX__PARTNER_LOCALIZATION_LOCAL : OMX__PARTNER_LOCALIZATION_REMOTE;

  return OMX_SUCCESS;
}

/*************
 * Connection
 */

void
omx__post_connect(struct omx_endpoint *ep,
		  struct omx__partner *partner,
		  union omx_request * req)
{
  struct omx_cmd_send_connect * connect_param = &req->connect.send_connect_ioctl_param;
  int err;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT, connect_param);
  if (err < 0) {
    omx_return_t ret = omx__errno_to_return("ioctl SEND_CONNECT");

    if (ret != OMX_NO_SYSTEM_RESOURCES)
      omx__abort("ioctl SEND_CONNECT returned unexpected error %m\n");

    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
}

/*
 * Start the connection process to another peer
 */
omx_return_t
omx__connect_common(omx_endpoint_t ep,
		    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
		    union omx_request * req)
{
  struct omx__partner * partner;
  struct omx_cmd_send_connect * connect_param = &req->connect.send_connect_ioctl_param;
  struct omx__connect_request_data * data_n = (void *) &connect_param->data;
  uint8_t connect_seqnum;
  omx_return_t ret;

  ret = omx__partner_lookup_by_addr(ep, nic_id, endpoint_id, &partner);
  if (ret != OMX_SUCCESS)
    goto out;

  connect_seqnum = partner->connect_seqnum++;
  req->generic.resends = 0;

  connect_param->hdr.peer_index = partner->peer_index;
  connect_param->hdr.dest_endpoint = partner->endpoint_index;
#ifdef OMX_DISABLE_SHARED
  connect_param->hdr.shared_disabled = 1;
#else
  connect_param->hdr.shared_disabled = !omx__globals.sharedcomms;
#endif
  connect_param->hdr.seqnum = 0;
  connect_param->hdr.length = sizeof(*data_n);
  OMX_PKT_FIELD_FROM(data_n->src_session_id, ep->desc->session_id);
  OMX_PKT_FIELD_FROM(data_n->app_key, key);
  OMX_PKT_FIELD_FROM(data_n->target_recv_seqnum_start, partner->next_match_recv_seq);
  OMX_PKT_FIELD_FROM(data_n->is_reply, 0);
  OMX_PKT_FIELD_FROM(data_n->connect_seqnum, connect_seqnum);

  omx__post_connect(ep, partner, req);

  /* no need to wait for a done event, tiny is synchronous */
  req->generic.state |= OMX_REQUEST_STATE_NEED_REPLY;
  omx__enqueue_request(&ep->connect_req_q, req);
  omx__enqueue_partner_connect_request(partner, req);

  req->generic.partner = partner;
  req->generic.resends_max = ep->req_resends_max;
  req->connect.session_id = ep->desc->session_id;
  req->connect.connect_seqnum = connect_seqnum;

  omx__progress(ep);

  return OMX_SUCCESS;

 out:
  return ret;
}

omx_return_t
omx_connect(omx_endpoint_t ep,
	    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	    uint32_t timeout,
	    omx_endpoint_addr_t *addr)
{
  union omx_request * req;
  omx_return_t ret;

  req = omx__request_alloc(ep);
  if (!req) {
    ret = OMX_NO_RESOURCES;
    goto out;
  }

  req->generic.type = OMX_REQUEST_TYPE_CONNECT;
  req->generic.state = OMX_REQUEST_STATE_INTERNAL; /* the state of synchronous connect is always initialized here */

  ret = omx__connect_common(ep, nic_id, endpoint_id, key, req);
  if (ret != OMX_SUCCESS)
    goto out_with_req;

  omx__debug_printf(CONNECT, "waiting for connect reply\n");
  ret = omx__connect_wait(ep, req, timeout);
  omx__debug_printf(CONNECT, "connect done\n");

  if (ret == OMX_SUCCESS) {
    switch (req->generic.status.code) {
    case OMX_STATUS_SUCCESS:
      omx__partner_to_addr(req->generic.partner, addr);
      ret = OMX_SUCCESS;
      break;
    case OMX_STATUS_BAD_KEY:
      ret = OMX_BAD_CONNECTION_KEY;
      break;
    case OMX_STATUS_ENDPOINT_CLOSED:
    case OMX_STATUS_BAD_ENDPOINT:
      ret = OMX_CONNECTION_FAILED;
      break;
    default:
      omx__abort("Failed to handle connect status %s\n",
		 omx_strstatus(req->generic.status.code));
    }
  }

  omx__request_free(ep, req);
  return ret;

 out_with_req:
  omx__request_free(ep, req);
 out:
  return ret;
}

omx_return_t
omx_iconnect(omx_endpoint_t ep,
	     uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	     uint64_t match_info,
	     void *context, omx_request_t *requestp)
{
  union omx_request * req;
  omx_return_t ret;

  req = omx__request_alloc(ep);
  if (!req) {
    ret = OMX_NO_RESOURCES;
    goto out;
  }

  req->generic.type = OMX_REQUEST_TYPE_CONNECT;
  req->generic.state = 0; /* iconnect is not INTERNAL */ /* the state of Asynchronous Iconnect is always initialized here */
  req->generic.status.match_info = match_info;
  req->generic.status.context = context;

  ret = omx__connect_common(ep, nic_id, endpoint_id, key, req);
  if (ret != OMX_SUCCESS)
    goto out_with_req;

  if (requestp) {
    *requestp = req;
  } else {
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
    ep->zombies++;
  }

  return ret;

 out_with_req:
  omx__request_free(ep, req);
 out:
  return ret;
}

/*
 * Complete the connect request
 */
void
omx__connect_complete(struct omx_endpoint *ep,
		      union omx_request * req, omx_status_code_t status)
{
  struct omx__partner *partner = req->generic.partner;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);

  omx__dequeue_request(&ep->connect_req_q, req);
  omx__dequeue_partner_connect_request(partner, req);
  req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;

  if (likely(req->generic.status.code == OMX_STATUS_SUCCESS))
    /* only set the status if it is not already set to an error */
    req->generic.status.code = status;

  if (status == OMX_STATUS_SUCCESS)
    omx__partner_to_addr(partner, &req->generic.status.addr);

  /* move iconnect request to the done queue */
  omx__notify_request_done(ep, ctxid, req);
}

/*
 * End the connection process to another peer
 */
static INLINE omx_return_t
omx__process_recv_connect_reply(struct omx_endpoint *ep,
				struct omx_evt_recv_connect *event)
{
  struct omx__partner * partner;
  struct omx__connect_reply_data * reply_data_n = (void *) event->data;
  uint32_t src_session_id = OMX_FROM_PKT_FIELD(reply_data_n->src_session_id);
  uint8_t connect_seqnum = OMX_FROM_PKT_FIELD(reply_data_n->connect_seqnum);
  uint32_t target_session_id = OMX_FROM_PKT_FIELD(reply_data_n->target_session_id);
  uint16_t target_recv_seqnum_start = OMX_FROM_PKT_FIELD(reply_data_n->target_recv_seqnum_start);
  uint8_t status_code = OMX_FROM_PKT_FIELD(reply_data_n->status_code);
  union omx_request * req;
  omx_return_t ret;

  ret = omx__partner_lookup(ep, event->peer_index, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_INVALID_PARAMETER)
      fprintf(stderr, "Open-MX: Received connect from unknown peer\n");
    return ret;
  }

  omx__partner_check_localization(partner, event->shared);

  omx__foreach_request(&ep->connect_req_q, req) {
    /* check the endpoint session (so that the endpoint didn't close/reopen in the meantime)
     * and the partner and the connection seqnum given by this partner
     */
    if (src_session_id == ep->desc->session_id
	&& partner == req->generic.partner
	&& connect_seqnum == req->connect.connect_seqnum) {
      goto found;
    }
  }

  /* invalid connect reply, just ignore it */
  return OMX_SUCCESS;

 found:
  omx__debug_printf(CONNECT, "waking up on connect reply\n");

  /* complete the request */
  omx__connect_complete(ep, req, status_code);

  /* update the partner afterwards, so that omx__partner_cleanup() does not find the current request too */
  if (status_code == OMX_STATUS_SUCCESS) {
    /* connection successfull, initialize stuff */

    omx__debug_printf(CONNECT, "got a connect reply with session id %lx while we have true %lx back %lx\n",
		      (unsigned long) target_session_id,
		      (unsigned long) partner->true_session_id, (unsigned long) partner->back_session_id);
    if (partner->back_session_id != target_session_id
	&& partner->back_session_id != -1) {
      /* this partner changed since last time it talked to us, cleanup the stuff */
      omx__debug_assert(partner->true_session_id != target_session_id);

      omx__partner_cleanup(ep, partner, 0);
    }

    if (partner->true_session_id != target_session_id) {
      /* either the first connect, or a new instance, reset seqnums */
      partner->next_send_seq = target_recv_seqnum_start;
      partner->next_acked_send_seq = target_recv_seqnum_start;
    }

    partner->true_session_id = target_session_id;
  }

  return OMX_SUCCESS;
}

/*
 * Another peer is connecting to us
 */
static INLINE omx_return_t
omx__process_recv_connect_request(struct omx_endpoint *ep,
				  struct omx_evt_recv_connect *event)
{
  struct omx__partner * partner;
  struct omx_cmd_send_connect reply_param;
  struct omx__connect_request_data * request_data_n = (void *) event->data;
  struct omx__connect_reply_data * reply_data_n = (void *) reply_param.data;
  uint32_t app_key = OMX_FROM_PKT_FIELD(request_data_n->app_key);
  uint32_t src_session_id = OMX_FROM_PKT_FIELD(request_data_n->src_session_id);
  uint16_t target_recv_seqnum_start = OMX_FROM_PKT_FIELD(request_data_n->target_recv_seqnum_start);
  omx_return_t ret;
  omx_status_code_t status_code;
  int err;

  ret = omx__partner_lookup(ep, event->peer_index, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_INVALID_PARAMETER)
      fprintf(stderr, "Open-MX: Received connect from unknown peer\n");
    return ret;
  }

  omx__partner_check_localization(partner, event->shared);

  if (app_key == ep->app_key) {
    status_code = OMX_STATUS_SUCCESS;
  } else {
    status_code = OMX_STATUS_BAD_KEY;
  }

  omx__debug_printf(CONNECT, "got a connect request with session id %lx while we have true %lx back %lx\n",
		    (unsigned long) src_session_id,
		    (unsigned long) partner->true_session_id, (unsigned long) partner->back_session_id);
  if (partner->back_session_id != src_session_id
      && partner->true_session_id != -1
      && partner->true_session_id != src_session_id) {
    /* new instance of the partner */

    omx__debug_printf(CONNECT, "connect from a new instance of a partner\n");

    partner->next_match_recv_seq = OMX__SEQNUM(0);
    partner->next_frag_recv_seq = OMX__SEQNUM(0);
    omx__partner_cleanup(ep, partner, 0);
  }

  if (partner->true_session_id != src_session_id) {
    /* we were connected to this partner, and it changed, reset the seqnums */
    partner->next_send_seq = target_recv_seqnum_start;
    partner->next_acked_send_seq = target_recv_seqnum_start;
  }

  partner->true_session_id  = src_session_id;
  partner->back_session_id  = src_session_id;

  reply_param.hdr.peer_index = partner->peer_index;
  reply_param.hdr.dest_endpoint = partner->endpoint_index;
#ifdef OMX_DISABLE_SHARED
  reply_param.hdr.shared_disabled = 1;
#else
  reply_param.hdr.shared_disabled = !omx__globals.sharedcomms;
#endif
  reply_param.hdr.seqnum = 0;
  reply_param.hdr.length = sizeof(*reply_data_n);
  reply_data_n->src_session_id = request_data_n->src_session_id;
  OMX_PKT_FIELD_FROM(reply_data_n->target_session_id, ep->desc->session_id);
  OMX_PKT_FIELD_FROM(reply_data_n->target_recv_seqnum_start, partner->next_match_recv_seq);
  OMX_PKT_FIELD_FROM(reply_data_n->is_reply, 1);
  reply_data_n->connect_seqnum = request_data_n->connect_seqnum;
  OMX_PKT_FIELD_FROM(reply_data_n->status_code, status_code);

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

/***************************
 * Endpoint address context
 */

omx_return_t
omx_set_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void *context)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);
  partner->user_context = context;
  return OMX_SUCCESS;
}

omx_return_t
omx_get_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void **context)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);
  *context = partner->user_context;
  return OMX_SUCCESS;
}

/*******************************
 * Disconnecting from a partner
 */

void
omx__partner_cleanup(struct omx_endpoint *ep, struct omx__partner *partner, int disconnect)
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  union omx_request *req, *next;
  uint32_t ctxid;
  int count;

  omx__board_addr_sprintf(board_addr_str, partner->board_addr);
  printf("Cleaning partner %s endpoint %d\n", board_addr_str, partner->endpoint_index);

  /*
   * Complete pending send/recv with an error status (they should get nacked earlier most of the times).
   * Take them from the partner non-acked queue, it will remove them from either
   * the endpoint requeued_send_req_q or non_acked_req_q.
   * And mediums that are currently in the driver will get their status marked as wrong
   * and they will be completed with this status when leaving driver_posted_req_q.
   */
  count = 0;
  omx__foreach_partner_non_acked_request_safe(partner, req, next) {
    omx__debug_printf(CONNECT, "Dropping pending send %p with seqnum %d\n", req, (unsigned) req->generic.send_seqnum);
    omx__dequeue_partner_non_acked_request(partner, req);
    omx__mark_request_acked(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    printf("Dropped %d pending send requests to partner\n", count);

  /*
   * Complete send large that were acked without being notified.
   */
  count = 0;
  omx__foreach_request_safe(&ep->large_send_req_q, req, next) {
    if (req->generic.partner != partner)
      continue;
    omx__debug_printf(CONNECT, "Dropping need-reply large send %p\n", req);
    omx__dequeue_request(&ep->large_send_req_q, req);
    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_REPLY);
    req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;
    omx__send_complete(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    printf("Dropped %d need-reply large sends to partner\n", count);

  /*
   * No need to look at the endpoint pull_req_q, they will be nacked or timeout in the driver anyway.
   */

  /*
   * Drop queued send requests.
   */
  count = 0;
  omx__foreach_request_safe(&ep->queued_send_req_q, req, next) {
    if (req->generic.partner != partner)
      continue;

    omx__dequeue_request(&ep->queued_send_req_q, req);
    req->generic.state &= ~OMX_REQUEST_STATE_QUEUED;
    omx__debug_printf(CONNECT, "Dropping queued send %p\n", req);

    switch (req->generic.type) {
    case OMX_REQUEST_TYPE_SEND_MEDIUM:
      /* no sendq slot has been allocated, make sure none will be released and complete the request */
      req->send.specific.medium.frags_nr = 0;
      omx__send_complete(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);
      break;
    case OMX_REQUEST_TYPE_SEND_LARGE:
      /* no region has been allocated, just complete the request */
      omx__send_complete(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);      
      break;
    case OMX_REQUEST_TYPE_RECV_LARGE:
       if (req->generic.state & OMX_REQUEST_STATE_RECV_PARTIAL) {
        /* pull request needs to the pushed to the driver, no region allocated yet, just complete the request */
	req->generic.state &= OMX_REQUEST_STATE_RECV_PARTIAL;
	omx__recv_complete(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);
      } else {
        /* the pull is already done, just drop the notify */
	omx__recv_complete(ep, req, OMX_REQUEST_STATE_RECV_PARTIAL);
       }
      break;
    default:
      omx__abort("Failed to handle queued request with type %d\n",
                 req->generic.type);
    }

    count++;
  }
  if (count)
    printf("Dropped %d queued sends to partner\n", count);

  /*
   * Drop pending connect request to this partner.
   * Take them from the partner connect queue, it will remove them
   * from the endpoint connect_req_q.
   */
  count = 0;
  while (!omx__partner_connect_queue_empty(partner)) {
    req = omx__partner_connect_queue_first_request(partner);
    omx__debug_printf(CONNECT, "Dropping pending connect %p\n", req);
    omx__connect_complete(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    printf("Dropped %d pending connect request to partner\n", count);

  /*
   * Complete partially received request with an error status
   * Take them from the partner partial queue, it will remove them
   * from the endpoint multifrag_medium_recv_req_q or unexp_req_q.
   */
  count = 0;
  while (!omx__partner_partial_queue_empty(partner)) {
    uint32_t ctxid;
    req = omx__partner_partial_queue_first_request(partner);
    ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);

    omx__debug_printf(CONNECT, "Dropping partial medium recv %p\n", req);

    /* dequeue and complete with status error */
    omx__dequeue_partner_partial_request(partner, req);
    omx__dequeue_request(unlikely(req->generic.state & OMX_REQUEST_STATE_RECV_UNEXPECTED)
                         ? &ep->ctxid[ctxid].unexp_req_q : &ep->multifrag_medium_recv_req_q,
                         req);
    req->generic.state &= ~OMX_REQUEST_STATE_RECV_PARTIAL;
    omx__recv_complete(ep, req, OMX_STATUS_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    printf("Dropped %d partially received messages from partner\n", count);

  /*
   * Drop early fragments from the partner early queue.
   */
  count = 0;
  while (!omx__partner_early_queue_empty(partner)) {
    struct omx__early_packet *early = omx__partner_first_early_packet(partner);

    omx__debug_printf(CONNECT, "Dropping early fragment %p\n", early);

    omx__dequeue_partner_early_packet(partner, early);
    if (early->data)
      free(early->data);
    free(early);
    count++;
  }
  if (count)
    printf("Dropped %d early received packets from partner\n", count);

  /*
   * Drop unexpected from this peer.
   * Take them in the endpoint unexp_req_q.
   */
  count = 0;
  for(ctxid=0; ctxid < ep->ctxid_max; ctxid++) {
    struct list_head *head = &ep->ctxid[ctxid].unexp_req_q;
    list_for_each_entry_safe(req, next, head, generic.queue_elt) {
      if (req->generic.partner != partner)
        continue;

      omx__debug_printf(CONNECT, "Dropping unexpected recv %p\n", req);

      /* drop it and that's it */
      omx__dequeue_request(head, req);
      if (req->generic.type != OMX_REQUEST_TYPE_RECV_LARGE
	  && req->generic.status.msg_length > 0)
	/* release the single segment used for unexp buffer */
	free(req->recv.segs.single.ptr);
      omx__request_free(ep, req);

      count++;
    }
  }
  if (count)
    printf("Dropped %d unexpected message from partner\n", count);

  /*
   * Reset everything else to zero
   */
  omx__partner_reset(partner);

  /*
   * Change recv_seq to something very different for safety
   */
  if (disconnect) {
    partner->next_match_recv_seq ^= OMX__SEQNUM(0xb0f0) ;
    partner->next_frag_recv_seq ^= OMX__SEQNUM(0xcf0f);
  }
}

omx_return_t
omx_disconnect(omx_endpoint_t ep, omx_endpoint_addr_t addr)
{
  struct omx__partner * partner;

  omx__progress(ep);
  partner = omx__partner_from_addr(&addr);
  omx__partner_cleanup(ep, partner, 1);

  return OMX_SUCCESS;
}
