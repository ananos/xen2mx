/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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
#include "omx_segments.h"

/*
 * Partner session ids management:
 *
 * Each endpoint gets a random session identity number from the driver.
 * The endpoint key/filter is only used to filter connect requests in
 * the library. All other messages will be filtered by the driver/mcp
 * through this session number, which won't be the same across multiple
 * runs. All remote peers pass their session id to us during the connect
 * handshake.
 *
 * Each partner structure actually stores two session numbers.
 * + true_session_id is the regular one that we get back in the connect
 *   reply and may be used for normal sends.
 * + back_session_id is obtained from the connect request of the remote
 *   peer and we will use for special "back" communications such as acks
 *   and notify after large messages.
 *
 * Both true_ and back_session_id are set when we receive a connect
 * reply. Only back_session_id is set on connect request.
 * So back_session_id may be set without true_session_id, while
 * true_session_id being set implies that back_session_id is set to
 * the same.
 *
 * Updating rules for true_ and back_session_id:
 * + If true_session_id changes on reply/request, this is the first
 *   connect (true_session_id was unset), or the remote peer has
 *   changed (true_session_id was already set), we reset our send
 *   seqnums to what the remote peer wants to receive.
 * + If back_session_id changes on reply, this is the first reply
 *   (back_session_id was unset), or the remote peer has changed
 *   (back_session_id was already set), we reset our receive seqnums.
 * + If back_session_id changes on reply/request (and it was already
 *   set), the remote peer has changed, we cleanup our stuff. It also
 *   implies that true_session_id was unset or would change.
 */

/******************************
 * Endpoint address management
 */

/* API omx_get_endpoint_addr */
omx_return_t
omx_get_endpoint_addr(omx_endpoint_t endpoint,
		      omx_endpoint_addr_t *endpoint_addr)
{
  /* no need to lock here, there's no possible race condition or so */

  omx__partner_session_to_addr(endpoint->myself, endpoint->desc->session_id, endpoint_addr);
  return OMX_SUCCESS;
}

/* API omx_decompose_endpoint_addr */
omx_return_t
omx_decompose_endpoint_addr(omx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);

  /* no need to lock here, there's no possible race condition or so */

  *nic_id = partner->board_addr;
  *endpoint_id = partner->endpoint_index;
  return OMX_SUCCESS;
}

/* API omx_decompose_endpoint_addr_with_session */
omx_return_t
omx_decompose_endpoint_addr_with_session(omx_endpoint_addr_t endpoint_addr,
					 uint64_t *nic_id, uint32_t *endpoint_id, uint32_t *session_id)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);

  /* no need to lock here, there's no possible race condition or so */

  *nic_id = partner->board_addr;
  *endpoint_id = partner->endpoint_index;
  *session_id = ((struct omx__endpoint_addr *) &endpoint_addr)->session_id;
  return OMX_SUCCESS;
}

/*********************
 * Partner management
 */

static void
omx__partner_reset(struct omx__partner *partner)
{
  INIT_LIST_HEAD(&partner->non_acked_req_q);
  INIT_LIST_HEAD(&partner->connect_req_q);
  INIT_LIST_HEAD(&partner->partial_medium_recv_req_q);
  INIT_LIST_HEAD(&partner->early_recv_q);
  INIT_LIST_HEAD(&partner->need_seqnum_send_req_q);

  BUILD_BUG_ON(sizeof(omx__seqnum_t) != sizeof(((struct omx_pkt_msg *)NULL)->lib_seqnum));
  BUILD_BUG_ON(OMX__SESNUM_BITS+OMX__SEQNUM_BITS > 8*sizeof(omx__seqnum_t));

  partner->true_session_id = -1; /* will be initialized when we will be connected to the peer */
  partner->back_session_id = -1; /* will be initialized when the partner will connect to me */
  partner->next_send_seq = -1; /* will be initialized when the partner will reply to my connect */
  partner->next_acked_send_seq = -1; /* will be initialized when the partner will reply to my connect */
  OMX__SEQNUM_RESET(partner->next_match_recv_seq); /* will force the sender's send seq through the connect */
  partner->next_frag_recv_seq = partner->next_match_recv_seq; /* will force the sender's send seq through the connect */
  partner->last_acked_recv_seq = partner->next_frag_recv_seq; /* nothing to ack yet */
  partner->connect_seqnum = 0;
  partner->last_send_acknum = 0;
  partner->last_recv_acknum = 0;
  partner->throttling_sends_nr = 0;

  if (partner->need_ack != OMX__PARTNER_NEED_NO_ACK) {
    partner->need_ack = OMX__PARTNER_NEED_NO_ACK;
    list_del(&partner->endpoint_partners_to_ack_elt);
  }
}

static omx_return_t
omx__partner_create(struct omx_endpoint *ep, uint16_t peer_index,
		    uint64_t board_addr, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  struct omx__partner * partner;
  uint32_t partner_index;

  partner = omx_malloc(sizeof(*partner));
  if (unlikely(!partner))
    /* let the caller handle the error if retransmission cannot recover this */
    return OMX_NO_RESOURCES;

  partner->board_addr = board_addr;
  partner->endpoint_index = endpoint_index;
  partner->peer_index = peer_index;
  partner->localization = OMX__PARTNER_LOCALIZATION_UNKNOWN; /* will be set by omx__partner_check_localization() */
  partner->next_match_recv_seq = 0; /* first session, seqnum will be initialized by omx__partner_reset() */
  partner->need_ack = OMX__PARTNER_NEED_NO_ACK;

  omx__partner_reset(partner);

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__driver_desc->endpoint_max;
  ep->partners[partner_index] = partner;

  *partnerp = partner;
  omx__debug_printf(CONNECT, ep, "created partner %016llx ep %d peer index %d\n",
		    (unsigned long long) board_addr, (unsigned) endpoint_index, (unsigned) peer_index);

  return OMX_SUCCESS;
}

static INLINE void
omx__partner_check_localization(const struct omx_endpoint * ep, struct omx__partner * partner, int shared)
{
  enum omx__partner_localization localization;

#ifdef OMX_DISABLE_SHARED
  if (shared)
    omx__debug_printf(CONNECT, ep, "Driver reporting shared peer while shared support is disabled in the lib\n");
  localization = OMX__PARTNER_LOCALIZATION_REMOTE;
#else
  localization = shared ? OMX__PARTNER_LOCALIZATION_LOCAL : OMX__PARTNER_LOCALIZATION_REMOTE;
#endif

  if (partner->localization == OMX__PARTNER_LOCALIZATION_UNKNOWN) {
    partner->localization = localization;
    partner->rndv_threshold = shared ? omx__globals.shared_rndv_threshold : omx__globals.rndv_threshold;
    if (shared)
      omx__debug_printf(CONNECT, ep, "Using shared communication for partner %016llx ep %d\n",
			(unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index);
  } else {
    omx__debug_assert(partner->localization == localization);
  }
}

static omx_return_t
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
      omx__printf(ep, "Failed to find peer address of index %d (%s)\n",
		  (unsigned) peer_index, omx_strerror(ret));
      /* let the caller handle this */
      return ret;
    }

    return omx__partner_create(ep, peer_index, board_addr, endpoint_index, partnerp);
  }

  *partnerp = ep->partners[partner_index];
  return OMX_SUCCESS;
}

static omx_return_t
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
    omx__printf(ep, "Failed to find peer index of board %s (%s)\n",
		board_addr_str, omx_strerror(ret));
    /* let the caller handle this */
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
omx__connect_myself(struct omx_endpoint *ep)
{
  uint16_t peer_index;
  omx_return_t ret;
  int maybe_self = 0;
  int maybe_shared = 0;

  ret = omx__peer_addr_to_index(ep->board_info.addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    omx__printf(ep, "Failed to find peer index of local board %s (%s)\n",
		ep->board_addr_str, omx_strerror(ret));
    /* let open_endpoint handle the error */
    return ret;
  }

  ret = omx__partner_create(ep, peer_index,
			    ep->board_info.addr, ep->endpoint_index,
			    &ep->myself);
  if (ret != OMX_SUCCESS)
    /* let open_endpoint handle the error */
    return ret;

  /* connect seqnums and sessions for real */
  ep->myself->next_send_seq = OMX__SEQNUM(1);
  ep->myself->next_acked_send_seq = OMX__SEQNUM(1);
  ep->myself->next_match_recv_seq = OMX__SEQNUM(1);
  ep->myself->next_frag_recv_seq = OMX__SEQNUM(1);
  ep->myself->true_session_id = ep->desc->session_id;
  ep->myself->back_session_id = ep->desc->session_id;

#ifndef OMX_DISABLE_SELF
  maybe_self = omx__globals.selfcomms;
#endif
#ifndef OMX_DISABLE_SHARED
  maybe_shared = omx__globals.sharedcomms;
#endif
  ep->myself->localization = (maybe_self || maybe_shared) ? OMX__PARTNER_LOCALIZATION_LOCAL : OMX__PARTNER_LOCALIZATION_REMOTE;
  ep->myself->rndv_threshold = (maybe_self || maybe_shared) ? omx__globals.shared_rndv_threshold : omx__globals.rndv_threshold;

  omx__debug_printf(CONNECT, ep, "created myself partner %016llx ep %d peer index %d\n",
		    (unsigned long long) ep->board_info.addr, (unsigned) ep->endpoint_index, (unsigned) peer_index);

  return OMX_SUCCESS;
}

/*************
 * Connection
 */

void
omx__post_connect_request(const struct omx_endpoint *ep,
			  const struct omx__partner *partner,
			  union omx_request * req)
{
  struct omx_cmd_send_connect_request * connect_param = &req->connect.send_connect_request_ioctl_param;
  int err;

  connect_param->target_recv_seqnum_start = partner->next_match_recv_seq;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT_REQUEST, connect_param);
  if (err < 0) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "post connect request message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */
  }

  req->generic.resends++;
  req->generic.last_send_jiffies = omx__driver_desc->jiffies;
}

/*
 * Start the connection process to another peer
 */
static omx_return_t
omx__connect_common(omx_endpoint_t ep,
		    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
		    union omx_request * req)
{
  struct omx__partner * partner;
  struct omx_cmd_send_connect_request * connect_param = &req->connect.send_connect_request_ioctl_param;
  uint8_t connect_seqnum;
  omx_return_t ret;

  ret = omx__partner_lookup_by_addr(ep, nic_id, endpoint_id, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_PEER_NOT_FOUND)
      ret = OMX_NIC_ID_NOT_FOUND;
    ret = omx__error_with_ep(ep, ret, "Searching/Creating partner for connection");
    goto out;
  }

  req->generic.state |= OMX_REQUEST_STATE_NEED_REPLY;

#ifndef OMX_DISABLE_SELF
  if (partner == ep->myself) {
    req->generic.partner = ep->myself;
    omx__enqueue_request(&ep->connect_req_q, req);
    omx__enqueue_partner_request(&partner->connect_req_q, req);
    omx__connect_complete(ep, req, OMX_SUCCESS, ep->desc->session_id);

    /*
     * need to wakeup some possible connect-done waiters
     * since this event does not come from the driver
     */
    omx__notify_user_event(ep);

    return OMX_SUCCESS;
  }
#endif

  connect_seqnum = partner->connect_seqnum++;
  req->generic.resends = 0;

  connect_param->peer_index = partner->peer_index;
  connect_param->dest_endpoint = partner->endpoint_index;
#ifdef OMX_DISABLE_SHARED
  connect_param->shared_disabled = 1;
#else
  connect_param->shared_disabled = !omx__globals.sharedcomms;
#endif
  connect_param->seqnum = 0;
  connect_param->src_session_id = ep->desc->session_id;
  connect_param->app_key = key;
  connect_param->connect_seqnum = connect_seqnum;

  omx__post_connect_request(ep, partner, req);

  /* no need to wait for a done event, connect is synchronous */
  omx__enqueue_request(&ep->connect_req_q, req);
  omx__enqueue_partner_request(&partner->connect_req_q, req);

  req->generic.partner = partner;
  req->generic.resends_max = ep->req_resends_max;
  req->connect.session_id = ep->desc->session_id;
  req->connect.connect_seqnum = connect_seqnum;

  omx__progress(ep);

  return OMX_SUCCESS;

 out:
  return ret;
}

/* API omx_connect */
omx_return_t
omx_connect(omx_endpoint_t ep,
	    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	    uint32_t timeout,
	    omx_endpoint_addr_t *addr)
{
  union omx_request * req;
  omx_return_t ret;

  OMX__ENDPOINT_LOCK(ep);

  req = omx__request_alloc(ep);
  if (!req) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating connect request");
    goto out_with_lock;
  }

  req->generic.type = OMX_REQUEST_TYPE_CONNECT;
  req->generic.state = OMX_REQUEST_STATE_INTERNAL; /* synchronous connects are internal requests */

  ret = omx__connect_common(ep, nic_id, endpoint_id, key, req);
  if (ret != OMX_SUCCESS) {
    ret = omx__error_with_ep(ep, ret, "Allocating connect request");
    goto out_with_req;
  }

  omx__debug_printf(CONNECT, ep, "waiting for connect reply from partner %016llx ep %d\n",
		    (unsigned long long) nic_id, (unsigned) endpoint_id);
  ret = omx__connect_wait(ep, req, timeout);
  omx__debug_printf(CONNECT, ep, "connect done from partner %016llx ep %d\n",
		    (unsigned long long) nic_id, (unsigned) endpoint_id);

  if (ret == OMX_SUCCESS) {
    if (req->generic.status.code == OMX_SUCCESS) {
      *addr = req->generic.status.addr;
    } else {
      ret = req->generic.status.code;
      /* convert a request status into a proper routine return value,
       * since omx_connect does not expose its internal request.
       */
      if (ret == OMX_REMOTE_ENDPOINT_UNREACHABLE)
	ret = OMX_TIMEOUT;
      ret = omx__error_with_ep(ep, ret, "Completing connection");
    }
  } else {
    ret = omx__error_with_ep(ep, ret, "Waiting for connection to complete");
  }

  if (unlikely(!(req->generic.state & OMX_REQUEST_STATE_DONE))) {
    /* the request didn't complete, unqueue it before freeing */
    omx__dequeue_request(&ep->connect_req_q, req);
    omx__dequeue_partner_request(&req->generic.partner->connect_req_q, req);
  } else {
#ifdef OMX_LIB_DEBUG
    omx__dequeue_request(&ep->internal_done_req_q, req);
#endif
  }

  omx__request_free(ep, req);
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;

 out_with_req:
  omx__request_free(ep, req);
 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/* API omx_iconnect */
omx_return_t
omx_iconnect(omx_endpoint_t ep,
	     uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	     uint64_t match_info,
	     void *context, omx_request_t *requestp)
{
  union omx_request * req;
  omx_return_t ret;

  OMX__ENDPOINT_LOCK(ep);

  req = omx__request_alloc(ep);
  if (!req) {
    ret = omx__error_with_ep(ep, OMX_NO_RESOURCES, "Allocating iconnect request");
    goto out_with_lock;
  }

  req->generic.type = OMX_REQUEST_TYPE_CONNECT;
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

  OMX__ENDPOINT_UNLOCK(ep);
  return ret;

 out_with_req:
  omx__request_free(ep, req);
 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/*
 * Complete the connect request
 */
void
omx__connect_complete(struct omx_endpoint *ep,
		      union omx_request * req, omx_return_t status, uint32_t session_id)
{
  struct omx__partner *partner = req->generic.partner;
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);

  omx__dequeue_request(&ep->connect_req_q, req);
  omx__dequeue_partner_request(&partner->connect_req_q, req);
  req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;

  if (likely(req->generic.status.code == OMX_SUCCESS)) {
    /* only set the status if it is not already set to an error */

    /* call the request error handler for iconnect only, connect will call the main error handler later */
    if (!(req->generic.state & OMX_REQUEST_STATE_INTERNAL))
      req->generic.status.code = omx__error_with_req(ep, req, status, "Completing iconnect request");
    else
      req->generic.status.code = status;
  }

  if (status == OMX_SUCCESS)
    omx__partner_session_to_addr(partner, session_id, &req->generic.status.addr);

  /* move iconnect request to the done queue */
  omx__notify_request_done(ep, ctxid, req);
}

/*
 * Handle the reply message on the found corresponding connect request
 */
static INLINE void
omx__handle_connect_reply(struct omx_endpoint *ep,
			  struct omx__partner * partner,
			  union omx_request * req,
			  const struct omx_evt_recv_connect_reply * event)
{
  uint32_t target_session_id = event->target_session_id;
  uint32_t target_recv_seqnum_start = event->target_recv_seqnum_start;
  uint8_t connect_status_code = event->connect_status_code;
  omx_return_t status_code;

  switch (connect_status_code) {
  case OMX_CONNECT_STATUS_SUCCESS:
    status_code = OMX_SUCCESS;
    break;
  case OMX_CONNECT_STATUS_BAD_KEY:
    status_code = OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY;
    break;
  default:
    /* invalid connect_reply, just ignore it */
    return;
  }

  omx__debug_printf(CONNECT, ep, "waking up on connect reply from partner %016llx ep %d\n",
		    (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index);

  /* complete the request */
  omx__connect_complete(ep, req, status_code, target_session_id);

  /* update the partner afterwards, so that omx__partner_cleanup() does not find the current request too */
  if (status_code == OMX_SUCCESS) {
    /* connection successfull, initialize stuff */

    omx__debug_printf(CONNECT, ep, "got a connect reply with session id %lx while we have true %lx back %lx\n",
		      (unsigned long) target_session_id,
		      (unsigned long) partner->true_session_id, (unsigned long) partner->back_session_id);
    if (partner->back_session_id != target_session_id
	&& partner->back_session_id != -1) {
      /* this partner changed since last time it talked to us, cleanup the stuff */
      omx__debug_assert(partner->true_session_id != target_session_id);

      omx__printf(ep, "Got a connect reply from a new instance of a partner, cleaning old partner status\n");
      omx__partner_cleanup(ep, partner, 0);
    }

    if (partner->true_session_id != target_session_id) {
      /* we were connected to this partner, and it changed, reset our send seqnums */
      omx__debug_printf(SEQNUM, ep, "connect reply (with new session id) requesting next send seqnum %d (#%d)\n",
			(unsigned) OMX__SEQNUM(target_recv_seqnum_start),
			(unsigned) OMX__SESNUM_SHIFTED(target_recv_seqnum_start));
      partner->next_send_seq = target_recv_seqnum_start;
      partner->next_acked_send_seq = target_recv_seqnum_start;
    }

    partner->true_session_id = target_session_id;
  }
}

/*
 * Find the connect request corresponding to a reply and call the handler
 */
void
omx__process_recv_connect_reply(struct omx_endpoint *ep,
				const struct omx_evt_recv_connect_reply *event)
{
  struct omx__partner * partner;
  uint32_t src_session_id = event->src_session_id;
  uint8_t connect_seqnum = event->connect_seqnum;
  union omx_request * req;
  omx_return_t ret;

  ret = omx__partner_lookup(ep, event->peer_index, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_PEER_NOT_FOUND)
      omx__printf(ep, "Received connect from unknown peer index %d ep %d\n",
		  (unsigned) event->peer_index, (unsigned) event->src_endpoint);
    return;
  }

  omx__partner_check_localization(ep, partner, event->shared);

  omx__foreach_partner_request(&partner->connect_req_q, req) {
    /* check the endpoint session (so that the endpoint didn't close/reopen in the meantime)
     * and the partner and the connection seqnum given by this partner
     */
    if (src_session_id == ep->desc->session_id
	&& connect_seqnum == req->connect.connect_seqnum) {
      omx__handle_connect_reply(ep, partner, req, event);
      break;
    }
  }
}

/*
 * Another peer is connecting to us
 */
void
omx__process_recv_connect_request(struct omx_endpoint *ep,
				  const struct omx_evt_recv_connect_request *event)
{
  struct omx__partner * partner;
  struct omx_cmd_send_connect_reply reply_param;
  uint32_t app_key = event->app_key;
  uint32_t src_session_id = event->src_session_id;
  uint16_t target_recv_seqnum_start = event->target_recv_seqnum_start;
  uint8_t connect_status_code;
  omx_return_t ret;
  int err;

  ret = omx__partner_lookup(ep, event->peer_index, event->src_endpoint, &partner);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_PEER_NOT_FOUND)
      omx__printf(ep, "Received connect from unknown peer\n");
    /* just ignore the connect request */
    return;
  }

  omx__partner_check_localization(ep, partner, event->shared);

  if (app_key == ep->app_key) {
    connect_status_code = OMX_CONNECT_STATUS_SUCCESS;
  } else {
    connect_status_code = OMX_CONNECT_STATUS_BAD_KEY;
  }

  omx__debug_printf(CONNECT, ep, "got a connect request from partner %016llx ep %d with session id %lx while we have true %lx back %lx\n",
		    (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		    (unsigned long) src_session_id,
		    (unsigned long) partner->true_session_id, (unsigned long) partner->back_session_id);
  if (partner->back_session_id != src_session_id) {
    /* either a new (recv) instance, or the first one, we need to reset our recv seqnums */

    if (partner->back_session_id != -1) {
      /* this partner changed since last time it talked to us, cleanup the stuff */
      omx__printf(ep, "Got a connect from a new instance of a partner, cleaning old partner status\n");
      omx__partner_cleanup(ep, partner, 0);
    }

    /* setup recv seqnum */
    OMX__SEQNUM_RESET(partner->next_match_recv_seq); /* will force the sender's send seq through the connect */
    OMX__SEQNUM_RESET(partner->next_frag_recv_seq); /* will force the sender's send seq through the connect */
  }

  if (partner->true_session_id != src_session_id) {
    /* we were connected to this partner, and it changed, reset our send seqnums */
    omx__debug_printf(SEQNUM, ep, "connect request (with new session id) requesting next send seqnum %d (#%d)\n",
		      (unsigned) OMX__SEQNUM(target_recv_seqnum_start),
		      (unsigned) OMX__SESNUM_SHIFTED(target_recv_seqnum_start));
    partner->next_send_seq = target_recv_seqnum_start;
    partner->next_acked_send_seq = target_recv_seqnum_start;
  }

  partner->true_session_id  = src_session_id;
  partner->back_session_id  = src_session_id;

  reply_param.peer_index = partner->peer_index;
  reply_param.dest_endpoint = partner->endpoint_index;
#ifdef OMX_DISABLE_SHARED
  reply_param.shared_disabled = 1;
#else
  reply_param.shared_disabled = !omx__globals.sharedcomms;
#endif
  reply_param.seqnum = 0;
  reply_param.src_session_id = event->src_session_id;
  reply_param.target_session_id = ep->desc->session_id;
  reply_param.target_recv_seqnum_start = partner->next_match_recv_seq;
  reply_param.connect_seqnum = event->connect_seqnum;
  reply_param.connect_status_code = connect_status_code;

  err = ioctl(ep->fd, OMX_CMD_SEND_CONNECT_REPLY, &reply_param);
  if (err < 0) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "post connect reply");
    /* just ignore the error, the peer will resend the connect request */
  }
  /* no need to wait for a done event, connect is synchronous */
}

/***************************
 * Endpoint address context
 */

/* API omx_set_endpoint_addr_context */
omx_return_t
omx_set_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void *context)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);

  /* no need to lock here, there's no possible race condition or so */

  partner->user_context = context;
  return OMX_SUCCESS;
}

/* API omx_get_endpoint_addr_context */
omx_return_t
omx_get_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void **context)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);

  /* no need to lock here, there's no possible race condition or so */

  *context = partner->user_context;
  return OMX_SUCCESS;
}

/*******************************
 * Disconnecting from a partner
 */

/*
 * disconnect should be > 0 if we need to disconnect from peer,
 * and 2 if we also need to free the partner structure.
 */
void
omx__partner_cleanup(struct omx_endpoint *ep, struct omx__partner *partner, int disconnect)
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  union omx_request *req, *next;
  struct omx__early_packet *early, *next_early;
  int count;

  omx__board_addr_sprintf(board_addr_str, partner->board_addr);
  if (disconnect <= 1)
    omx__printf(ep, "Cleaning partner %s endpoint %d\n", board_addr_str, partner->endpoint_index);

  /*
   * Complete pending send/recv with an error status (they should get nacked earlier most of the times).
   * Take them from the partner non-acked queue, it will remove them from either
   * the endpoint resend_req_q or non_acked_req_q.
   * And mediums that are currently in the driver will get their status marked as wrong
   * and they will be completed with this status when leaving driver_posted_req_q.
   */
  count = 0;
  omx__foreach_partner_request_safe(&partner->non_acked_req_q, req, next) {
    omx__debug_printf(CONNECT, ep, "Dropping pending send %p with seqnum %d (#%d)\n", req,
		      (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
		      (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
    omx___dequeue_partner_request(req);
    omx__mark_request_acked(ep, req, OMX_REMOTE_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d pending send requests to partner\n", count);

  /*
   * Complete send large that were acked without being notified.
   */
  count = 0;
  omx__foreach_request_safe(&ep->large_send_need_reply_req_q, req, next) {
    if (req->generic.partner != partner)
      continue;
    omx__debug_printf(CONNECT, ep, "Dropping need-reply large send %p\n", req);
    omx___dequeue_request(req);
    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_REPLY);
    req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;
    omx__send_complete(ep, req, OMX_REMOTE_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d need-reply large sends to partner\n", count);

  /*
   * No need to look at the endpoint pull_req_q, they will be nacked or timeout in the driver anyway.
   */

  /*
   * Drop delayed need resources send requests.
   */
  count = 0;
  omx__foreach_request_safe(&ep->need_resources_send_req_q, req, next) {
    if (req->generic.partner != partner)
      continue;
    omx___dequeue_request(req);
    req->generic.state &= ~OMX_REQUEST_STATE_NEED_RESOURCES;
    omx__debug_printf(CONNECT, ep, "Dropping need-resources send %p\n", req);
    omx__complete_unsent_send_request(ep, req);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d need-resources sends to partner\n", count);

  /*
   * Drop need-seqnum send requests to this partner.
   * Just take them from the partner need_seqnum_send_req_q, they
   * are not queued anywhere else.
   */
  count = 0;
  omx__foreach_partner_request_safe(&partner->need_seqnum_send_req_q, req, next) {
    omx__debug_printf(CONNECT, ep, "Dropping need-seqnum send %p\n", req);
    omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_SEQNUM);
    omx___dequeue_partner_request(req);
    omx__complete_unsent_send_request(ep, req);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d need-seqnum send request to partner\n", count);

  /*
   * Drop pending connect request to this partner.
   * Take them from the partner connect queue, it will remove them
   * from the endpoint connect_req_q.
   */
  count = 0;
  omx__foreach_partner_request_safe(&partner->connect_req_q, req, next) {
    omx__debug_printf(CONNECT, ep, "Dropping pending connect %p\n", req);
    omx__connect_complete(ep, req, OMX_REMOTE_ENDPOINT_UNREACHABLE, (uint32_t) -1);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d pending connect request to partner\n", count);

  /*
   * Complete partially received request with an error status
   * Take them from the partner partial queue, it will remove them
   * from the endpoint multifrag_medium_recv_req_q or unexp_req_q.
   */
  count = 0;
  omx__foreach_partner_request_safe(&partner->partial_medium_recv_req_q, req, next) {
    uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);

    omx__debug_printf(CONNECT, ep, "Dropping partial medium recv %p\n", req);

    /* dequeue and complete with status error */
    omx___dequeue_partner_request(req);
    if(unlikely(req->generic.state & OMX_REQUEST_STATE_UNEXPECTED_RECV)) {
      omx__dequeue_request(&ep->anyctxid.unexp_req_q, req);
      if (unlikely(HAS_CTXIDS(ep)))
	omx__dequeue_ctxid_request(&ep->ctxid[ctxid].unexp_req_q, req);
#ifdef OMX_LIB_DEBUG
    } else {
      omx__dequeue_request(&ep->partial_medium_recv_req_q, req);
#endif
    }

    req->generic.state &= ~OMX_REQUEST_STATE_RECV_PARTIAL;
    omx__recv_complete(ep, req, OMX_REMOTE_ENDPOINT_UNREACHABLE);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d partially received messages from partner\n", count);

  /*
   * Drop early fragments from the partner early queue.
   */
  count = 0;
  omx__foreach_partner_early_packet_safe(partner, early, next_early) {
    omx___dequeue_partner_early_packet(early);
    omx__debug_printf(CONNECT, ep, "Dropping early fragment %p\n", early);

    omx_free(early->data);
    omx_free(early);
    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d early received packets from partner\n", count);

  /*
   * Drop unexpected from this peer.
   * Take them in the endpoint unexp_req_q.
   */
  count = 0;
  omx__foreach_request_safe(&ep->anyctxid.unexp_req_q, req, next) {
    if (req->generic.partner != partner)
      continue;

    omx__debug_printf(CONNECT, ep, "Dropping unexpected recv %p\n", req);

    /* drop it and that's it */
    omx___dequeue_request(req);
    if (req->generic.type != OMX_REQUEST_TYPE_RECV_LARGE
	&& req->generic.status.msg_length > 0)
      /* release the single segment used for unexp buffer */
      omx_free(OMX_SEG_PTR(&req->recv.segs.single));
    omx__request_free(ep, req);

    count++;
  }
  if (count)
    omx__printf(ep, "Dropped %d unexpected message from partner\n", count);

  /*
   * Reset everything else to zero
   */
  omx__partner_reset(partner);

  if (disconnect) {
    /*
     * Change recv_seq to something very different for safety
     */
    partner->next_match_recv_seq ^= OMX__SEQNUM(0xb0f0) ;
    partner->next_frag_recv_seq ^= OMX__SEQNUM(0xcf0f);
    partner->next_match_recv_seq += OMX__SESNUM_ONE;
    partner->next_frag_recv_seq += OMX__SESNUM_ONE;
    omx__debug_printf(SEQNUM, ep, "disconnect increasing session number to #%d\n",
		      (unsigned) OMX__SESNUM_SHIFTED(partner->next_match_recv_seq));

    if (disconnect > 1) {
      /*
       * The application requests a disconnect, so the corresponding endpoint address
       * is now invalid. Just drop the partner entirely, it will prevent messages
       * about future reconnections
       */
      uint32_t partner_index = ((uint32_t) partner->endpoint_index)
				+ ((uint32_t) partner->peer_index) * omx__driver_desc->endpoint_max;
      ep->partners[partner_index] = NULL;
      omx_free(partner);
    }
  }
}

/* API omx_disconnect */
omx_return_t
omx_disconnect(omx_endpoint_t ep, omx_endpoint_addr_t addr)
{
  struct omx__partner * partner;

  OMX__ENDPOINT_LOCK(ep);

  omx__progress(ep);
  partner = omx__partner_from_addr(&addr);
  omx__partner_cleanup(ep, partner, 2);

  OMX__ENDPOINT_UNLOCK(ep);
  return OMX_SUCCESS;
}
