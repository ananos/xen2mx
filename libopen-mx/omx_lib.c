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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "omx_io.h"
#include "omx_lib.h"
#include "omx_request.h"
#include "omx_lib_wire.h"
#include "omx_wire_access.h"

/*******************
 * Event processing
 */

static omx_return_t
omx__process_event(struct omx_endpoint * ep, union omx_evt * evt)
{
  omx_return_t ret = OMX_SUCCESS;

  omx__debug_printf(EVENT, "received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case OMX_EVT_RECV_CONNECT: {
    ret = omx__process_recv_connect(ep, &evt->recv_connect);
    break;
  }

  case OMX_EVT_RECV_TINY: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    ret = omx__process_recv(ep,
			    msg, msg->specific.tiny.data, msg->specific.tiny.length,
			    omx__process_recv_tiny);
    break;
  }

  case OMX_EVT_RECV_SMALL: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    char * recvq_buffer = ep->recvq + msg->specific.small.recvq_offset;
    ret = omx__process_recv(ep,
			    msg, recvq_buffer, msg->specific.small.length,
			    omx__process_recv_small);
    break;
  }

  case OMX_EVT_RECV_MEDIUM: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    char * recvq_buffer = ep->recvq + msg->specific.medium.recvq_offset;
    ret = omx__process_recv(ep,
			    msg, recvq_buffer, msg->specific.medium.msg_length,
			    omx__process_recv_medium_frag);
    break;
  }

  case OMX_EVT_RECV_RNDV: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    struct omx__rndv_data * data_n = (void *) msg->specific.rndv.data;
    uint32_t msg_length = OMX_FROM_PKT_FIELD(data_n->msg_length);
    ret = omx__process_recv(ep,
			    msg, NULL, msg_length,
			    omx__process_recv_rndv);
    break;
  }

  case OMX_EVT_RECV_NOTIFY: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    ret = omx__process_recv(ep,
			    msg, NULL, 0,
			    omx__process_recv_notify);
    break;
  }

  case OMX_EVT_SEND_MEDIUM_FRAG_DONE: {
    uint16_t sendq_page_offset = evt->send_medium_frag_done.sendq_page_offset;
    union omx_request * req = omx__endpoint_sendq_map_user(ep, sendq_page_offset);

    omx__debug_assert(req);
    omx__debug_assert(req->generic.type == OMX_REQUEST_TYPE_SEND_MEDIUM);

    ep->avail_exp_events++;

    /* message is not done */
    if (unlikely(--req->send.specific.medium.frags_pending_nr))
      break;

    req->generic.state &= ~OMX_REQUEST_STATE_IN_DRIVER;
    omx__dequeue_request(&ep->driver_posted_req_q, req);

    if (likely(req->generic.state & OMX_REQUEST_STATE_NEED_ACK))
      omx__enqueue_request(&ep->non_acked_req_q, req);
    else
      omx__send_complete(ep, req, OMX_STATUS_SUCCESS);

    break;
  }

  case OMX_EVT_PULL_DONE: {
    ep->avail_exp_events++;

    omx__process_pull_done(ep, &evt->pull_done);
    break;
  }

  case OMX_EVT_RECV_TRUC: {
    ret = omx__process_recv_truc(ep, &evt->recv_truc);
    break;
  }

  case OMX_EVT_RECV_NACK_LIB: {
    struct omx_evt_recv_nack_lib * nack_lib = &evt->recv_nack_lib;
    uint16_t peer_index = nack_lib->peer_index;
    uint16_t seqnum = nack_lib->seqnum;
    uint8_t nack_type = nack_lib->nack_type;
    struct omx__partner * partner;
    uint64_t board_addr = 0;
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx_status_code_t status;

    omx__partner_recv_lookup(ep, peer_index, nack_lib->src_endpoint,
			     &partner);
    if (unlikely(!partner)) {
      ret = OMX_SUCCESS;
      break;
    }

    omx__peer_index_to_addr(peer_index, &board_addr);
    omx__board_addr_sprintf(board_addr_str, board_addr);

    switch (nack_type) {
    case OMX_EVT_NACK_LIB_BAD_ENDPT:
      status = OMX_STATUS_BAD_ENDPOINT;
      break;
    case OMX_EVT_NACK_LIB_ENDPT_CLOSED:
      status = OMX_STATUS_ENDPOINT_CLOSED;
      break;
    case OMX_EVT_NACK_LIB_BAD_SESSION:
      status = OMX_STATUS_BAD_SESSION;
      break;
    default:
      omx__abort("Failed to handle NACK with unknown type (%d) from peer %s (index %d) seqnum %d\n",
		 (unsigned) nack_type, board_addr_str, (unsigned) peer_index, (unsigned) seqnum);
    }

    ret = omx__handle_nack(ep, partner, seqnum, status);
    break;
  }

  default:
    omx__abort("Failed to handle event with unknown type %d\n",
	       evt->generic.type);
  }

  return ret;
}

/**************
 * Progression
 */

static INLINE void
omx__check_endpoint_desc(struct omx_endpoint * ep)
{
  static uint64_t last_check = 0;
  uint64_t now = omx__driver_desc->jiffies;
  uint64_t driver_status;
  struct omx__partner *partner;

  /* check once every second */
  if (now - last_check < ep->check_status_delay_jiffies)
    return;

  driver_status = ep->desc->status;
  /* could be racy... could be fixed using atomic ops... */
  ep->desc->status = 0;

  if (!driver_status)
    return;

  if (driver_status & OMX_ENDPOINT_DESC_STATUS_EXP_EVENTQ_FULL) {
    omx__abort("Driver reporting expected event queue full\n");
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL) {
    printf("Driver reporting unexpected event queue full\n");
    printf("Some packets are being dropped, they will be resent by the sender\n");
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_DOWN) {
    printf("Open-MX WARNING: Driver reporting that interface %s (%s) for endpoint %d is NOT up, check dmesg\n",
	   ep->board_info.ifacename, ep->board_info.hostname, ep->endpoint_index);
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_BAD_MTU) {
    printf("Open-MX WARNING: Driver reporting too small MTU for interface %s (%s) for endpoint %d, check dmesg\n",
	   ep->board_info.ifacename, ep->board_info.hostname, ep->endpoint_index);
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_REMOVED) {
    omx__abort("Open-MX WARNING: Driver reporting endpoint %d being closed because interface %s (%s) has been removed\n",
	       ep->endpoint_index, ep->board_info.ifacename, ep->board_info.hostname);
    /* FIXME: find a nice way to exit here? */
  }

  list_for_each_entry(partner, &ep->throttling_partners_list, endpoint_throttling_partners_elt)
    printf("Open-MX: Partner not acking enough, throttling %d send requests\n", partner->throttling_sends_nr);

  last_check = now;
}

static INLINE void
omx__check_enough_progression(struct omx_endpoint * ep)
{
#ifdef OMX_LIB_DEBUG
  static unsigned long long last_progress = 0;
  unsigned long long now = omx__driver_desc->jiffies;
  unsigned long long delay = now - last_progress;

  if (last_progress && delay > omx__driver_desc->hz)
    omx__verbose_printf("No progression occured in the last %lld seconds (%lld jiffies)\n",
			delay/omx__driver_desc->hz, delay);

  last_progress = now;
#endif
}

omx_return_t
omx__progress(struct omx_endpoint * ep)
{
  if (unlikely(ep->progression_disabled))
    return OMX_SUCCESS;

  omx__check_enough_progression(ep);

  /* ack partners that didn't get acked recently */
  omx__process_partners_to_ack(ep);

  /* process unexpected events first,
   * to release the pressure coming from the network
   */
  while (1) {
    volatile union omx_evt * evt = ep->next_unexp_event;

    if (unlikely(evt->generic.type == OMX_EVT_NONE))
      break;

    omx__process_event(ep, (union omx_evt *) evt);

    /* mark event as done */
    evt->generic.type = OMX_EVT_NONE;

    /* next event */
    evt++;
    if (unlikely((void *) evt >= ep->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE))
      evt = ep->unexp_eventq;
    ep->next_unexp_event = (void *) evt;
  }

  /* process expected events then */
  while (1) {
    volatile union omx_evt * evt = ep->next_exp_event;

    if (unlikely(evt->generic.type == OMX_EVT_NONE))
      break;

    omx__process_event(ep, (union omx_evt *) evt);

    /* mark event as done */
    evt->generic.type = OMX_EVT_NONE;

    /* next event */
    evt++;
    if (unlikely((void *) evt >= ep->exp_eventq + OMX_EXP_EVENTQ_SIZE))
      evt = ep->exp_eventq;
    ep->next_exp_event = (void *) evt;
  }

  /* resend requests that didn't get acked/replied */
  omx__process_resend_requests(ep);

  /* post queued requests */
  omx__process_queued_requests(ep);

  /* check the endpoint descriptor */
  omx__check_endpoint_desc(ep);

  return OMX_SUCCESS;
}

/* API omx_register_unexp_handler */
omx_return_t
omx_register_unexp_handler(omx_endpoint_t ep,
			   omx_unexp_handler_t handler,
			   void *context)
{
  OMX__ENDPOINT_LOCK(ep);

  ep->unexp_handler = handler;
  ep->unexp_handler_context = context;

  OMX__ENDPOINT_UNLOCK(ep);
  return OMX_SUCCESS;
}

/* API omx_progress */
omx_return_t
omx_progress(omx_endpoint_t ep)
{
  omx_return_t ret = OMX_SUCCESS;

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);

  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

#ifdef OMX_LIB_DEBUG
static uint64_t omx_disable_progression_jiffies_start = 0;
#endif

/* API omx_disable_progression */
omx_return_t
omx_disable_progression(struct omx_endpoint *ep)
{
  omx_return_t ret = OMX_SUCCESS;

  OMX__ENDPOINT_LOCK(ep);

  if (ep->progression_disabled & OMX_PROGRESSION_DISABLED_BY_API) {
    /* progression is already disabled, just ignore */
    goto out_with_lock;
  }

  /* wait for the handler to be done */
  while (ep->progression_disabled & OMX_PROGRESSION_DISABLED_IN_HANDLER)
    OMX__ENDPOINT_HANDLER_DONE_WAIT(ep);

  ep->progression_disabled = OMX_PROGRESSION_DISABLED_BY_API;

#ifdef OMX_LIB_DEBUG
  omx_disable_progression_jiffies_start = omx__driver_desc->jiffies;
#endif

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/* API omx_reenable_progression */
omx_return_t
omx_reenable_progression(struct omx_endpoint *ep)
{
  OMX__ENDPOINT_LOCK(ep);

  if (!(ep->progression_disabled & OMX_PROGRESSION_DISABLED_BY_API)) {
    /* progression is already enabled, just ignore */
    goto out_with_lock;
  }

  ep->progression_disabled &= ~OMX_PROGRESSION_DISABLED_BY_API;

#ifdef OMX_LIB_DEBUG
  {
    uint64_t now = omx__driver_desc->jiffies;
    uint64_t delay = now - omx_disable_progression_jiffies_start;
    if (delay > omx__driver_desc->hz)
      omx__verbose_printf("Application disabled progression during %lld seconds (%lld jiffies)\n",
			  (unsigned long long) delay/omx__driver_desc->hz, (unsigned long long) delay);
  }
#endif

  omx__progress(ep);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return OMX_SUCCESS;
}
