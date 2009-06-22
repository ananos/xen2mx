/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2009 (see AUTHORS file)
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

/*******************
 * Event processing
 */

static void
omx__process_event(struct omx_endpoint * ep, const union omx_evt * evt)
{
  omx__debug_printf(EVENT, ep, "received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case OMX_EVT_RECV_CONNECT_REQUEST: {
    omx__process_recv_connect_request(ep, &evt->recv_connect_request);
    break;
  }

  case OMX_EVT_RECV_CONNECT_REPLY: {
    omx__process_recv_connect_reply(ep, &evt->recv_connect_reply);
    break;
  }

  case OMX_EVT_RECV_TINY: {
    const struct omx_evt_recv_msg * msg = &evt->recv_msg;
    omx__process_recv(ep,
		      msg, msg->specific.tiny.data, msg->specific.tiny.length,
		      omx__process_recv_tiny);
    break;
  }

  case OMX_EVT_RECV_SMALL: {
    const struct omx_evt_recv_msg * msg = &evt->recv_msg;
    char * recvq_buffer = ep->recvq + msg->specific.small.recvq_offset;
    omx__process_recv(ep,
		      msg, recvq_buffer, msg->specific.small.length,
		      omx__process_recv_small);
    break;
  }

  case OMX_EVT_RECV_MEDIUM_FRAG: {
    const struct omx_evt_recv_msg * msg = &evt->recv_msg;
    char * recvq_buffer = ep->recvq + msg->specific.medium_frag.recvq_offset;
    omx__process_recv(ep,
		      msg, recvq_buffer, msg->specific.medium_frag.msg_length,
		      omx__process_recv_medium_frag);
    break;
  }

  case OMX_EVT_RECV_RNDV: {
    const struct omx_evt_recv_msg * msg = &evt->recv_msg;
    uint32_t msg_length = msg->specific.rndv.msg_length;
    omx__process_recv(ep,
		      msg, NULL, msg_length,
		      omx__process_recv_rndv);
    break;
  }

  case OMX_EVT_RECV_NOTIFY: {
    const struct omx_evt_recv_msg * msg = &evt->recv_msg;
    omx__process_recv(ep,
		      msg, NULL, 0,
		      omx__process_recv_notify);
    break;
  }

  case OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE: {
    uint16_t sendq_index = evt->send_mediumsq_frag_done.sendq_offset >> OMX_SENDQ_ENTRY_SHIFT;
    union omx_request * req = omx__endpoint_sendq_map_user(ep, sendq_index);

    omx__debug_assert(req);
    omx__debug_assert(req->generic.type == OMX_REQUEST_TYPE_SEND_MEDIUMSQ);

    ep->avail_exp_events++;

    /* message is not done */
    if (unlikely(--req->send.specific.mediumsq.frags_pending_nr))
      break;

    req->generic.state &= ~OMX_REQUEST_STATE_DRIVER_MEDIUMSQ_SENDING;
    omx__dequeue_request(&ep->driver_mediumsq_sending_req_q, req);

    if (likely(req->generic.state & OMX_REQUEST_STATE_NEED_ACK))
      omx__enqueue_request(&ep->non_acked_req_q, req);
    else
      omx__send_complete(ep, req, OMX_SUCCESS);

    break;
  }

  case OMX_EVT_PULL_DONE: {
    ep->avail_exp_events++;

    omx__process_pull_done(ep, &evt->pull_done);
    break;
  }

  case OMX_EVT_RECV_LIBACK: {
    omx__process_recv_liback(ep, &evt->recv_liback);
    break;
  }

  case OMX_EVT_RECV_NACK_LIB: {
    omx__process_recv_nack_lib(ep, &evt->recv_nack_lib);
    break;
  }

  case OMX_EVT_IGNORE:
    break;

  default:
    omx__abort(ep, "Failed to handle event with unknown type %d\n",
	       evt->generic.type);
  }
}

/**************
 * Progression
 */

static INLINE void
omx__check_endpoint_desc(struct omx_endpoint * ep)
{
  uint64_t now = omx__driver_desc->jiffies;
  uint64_t last = ep->last_check_jiffies;
  uint64_t driver_status;
  struct omx__partner *partner;

  /* check once every second */
  if (now - last < ep->check_status_delay_jiffies)
    return;
  ep->last_check_jiffies = now;

  driver_status = ep->desc->status;
  /* could be racy... could be fixed using atomic ops... */
  ep->desc->status = 0;

  if (!driver_status)
    return;

  if (driver_status & OMX_ENDPOINT_DESC_STATUS_EXP_EVENTQ_FULL) {
    omx__abort(ep, "Driver reporting expected event queue full\n");
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL) {
    omx__verbose_printf(ep, "Driver reporting unexpected event queue full\n");
    omx__verbose_printf(ep, "Some packets are being dropped, they will be resent by the sender\n");
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_DOWN) {
    omx__warning(ep, "Driver reporting that interface %s (%s) for endpoint %d is NOT up, check dmesg\n",
		 ep->board_info.ifacename, ep->board_info.hostname, ep->endpoint_index);
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_BAD_MTU) {
    omx__warning(ep, "Driver reporting too small MTU for interface %s (%s) for endpoint %d, check dmesg\n",
		 ep->board_info.ifacename, ep->board_info.hostname, ep->endpoint_index);
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_REMOVED) {
    omx__abort(ep, "Driver reporting endpoint %d being closed because interface %s (%s) has been removed\n",
	       ep->endpoint_index, ep->board_info.ifacename, ep->board_info.hostname);
    /* FIXME: find a nice way to exit here? */
  }
  if (driver_status & OMX_ENDPOINT_DESC_STATUS_IFACE_HIGH_INTRCOAL) {
    omx__verbose_printf(ep, "Driver reporting very high interrupt coalescing for interface %s (%s) for endpoint %d, check dmesg\n",
			ep->board_info.ifacename, ep->board_info.hostname, ep->endpoint_index);
  }

  list_for_each_entry(partner, &ep->throttling_partners_list, endpoint_throttling_partners_elt)
    omx__verbose_printf(ep, "Partner not acking enough, throttling %d send requests\n", partner->throttling_sends_nr);
}

static INLINE void
omx__check_enough_progression(struct omx_endpoint * ep)
{
#ifdef OMX_LIB_DEBUG
  unsigned long long now = omx__driver_desc->jiffies;
  unsigned long long last = ep->last_progress_jiffies;
  unsigned long long delay = now - last;

  if (last && delay > omx__driver_desc->hz)
    omx__verbose_printf(ep, "No progression occured in the last %lld seconds (%lld jiffies)\n",
			delay/omx__driver_desc->hz, delay);

  ep->last_progress_jiffies = now;
#endif
}

omx_return_t
omx__progress(struct omx_endpoint * ep)
{
  if (unlikely(ep->progression_disabled))
    return OMX_SUCCESS;

  omx__check_enough_progression(ep);

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

  /* post delayed requests */
  omx__process_delayed_requests(ep);

  /* ack partners that didn't get acked recently */
  omx__process_partners_to_ack(ep);

  /* check the endpoint descriptor */
  omx__check_endpoint_desc(ep);

  /* check if we leaked some requests */
  if (omx__globals.check_request_alloc)
    omx__request_alloc_check(ep);

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
  return OMX_SUCCESS;
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
      omx__verbose_printf(ep, "Application disabled progression during %lld seconds (%lld jiffies)\n",
			  (unsigned long long) delay/omx__driver_desc->hz, (unsigned long long) delay);
  }
#endif

  omx__progress(ep);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return OMX_SUCCESS;
}
