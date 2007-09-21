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
#include <assert.h>

#include "omx_io.h"
#include "omx_lib.h"
#include "omx_request.h"

/*******************
 * Event processing
 */

static inline char *
omx__recvq_slot_of_unexp_eventq(struct omx_endpoint * ep, union omx_evt * evt)
{
  return ep->recvq + (((char *) evt - (char *) ep->unexp_eventq) << (OMX_RECVQ_ENTRY_SHIFT-OMX_EVENTQ_ENTRY_SHIFT));
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
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    ret = omx__process_recv(ep,
			    msg, msg->specific.tiny.data, msg->specific.tiny.length,
			    omx__process_recv_tiny);
    break;
  }

  case OMX_EVT_RECV_SMALL: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    char * recvq_buffer = omx__recvq_slot_of_unexp_eventq(ep, evt);
    ret = omx__process_recv(ep,
			    msg, recvq_buffer, msg->specific.small.length,
			    omx__process_recv_small);
    break;
  }

  case OMX_EVT_RECV_MEDIUM: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    char * recvq_buffer = omx__recvq_slot_of_unexp_eventq(ep, evt);
    ret = omx__process_recv(ep,
			    msg, recvq_buffer, msg->specific.medium.msg_length,
			    omx__process_recv_medium_frag);
    break;
  }

  case OMX_EVT_RECV_RNDV: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    uint32_t msg_length = *(uint32_t *) &(msg->specific.tiny.data[0]);
    ret = omx__process_recv(ep,
			    msg, NULL, msg_length,
			    omx__process_recv_rndv);
    break;
  }

  case OMX_EVT_RECV_NOTIFY: {
    struct omx_evt_recv_msg * msg = &evt->recv_msg;
    uint32_t xfer_length = msg->specific.notify.length;
    union omx_request * req = omx__queue_first_request(&ep->large_send_req_q);

    assert(req);
    assert(req->generic.type == OMX_REQUEST_TYPE_SEND_LARGE);

    omx__dequeue_request(&ep->large_send_req_q, req);
    omx__deregister_region(ep, req->send.specific.large.region);
    req->generic.status.xfer_length = xfer_length;

    req->generic.state &= ~OMX_REQUEST_STATE_PENDING;
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    omx__send_complete(ep, req, OMX_STATUS_SUCCESS);
    break;
  }

  case OMX_EVT_SEND_MEDIUM_FRAG_DONE: {
    uint16_t sendq_page_offset = evt->send_medium_frag_done.sendq_page_offset;
    union omx_request * req = omx__endpoint_sendq_map_put(ep, sendq_page_offset);

    assert(req);
    assert(req->generic.type == OMX_REQUEST_TYPE_SEND_MEDIUM);

    /* message is not done */
    if (--req->send.specific.medium.frags_pending_nr)
      break;

    omx__dequeue_request(&ep->sent_req_q, req);

    req->generic.state &= ~OMX_REQUEST_STATE_PENDING;
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    omx__send_complete(ep, req, OMX_STATUS_SUCCESS);
    break;
  }

  case OMX_EVT_PULL_DONE: {
    omx__pull_done(ep, &evt->pull_done);
    break;
  }

  default:
    printf("unknown type\n");
    assert(0);
  }

  return ret;
}

/**************
 * Progression
 */

omx_return_t
omx__progress(struct omx_endpoint * ep)
{
  if (ep->in_handler)
    return OMX_SUCCESS;

  /* process unexpected events first,
   * to release the pressure coming from the network
   */
  while (1) {
    volatile union omx_evt * evt = ep->next_unexp_event;

    if (evt->generic.type == OMX_EVT_NONE)
      break;

    omx__process_event(ep, (union omx_evt *) evt);

    /* mark event as done */
    evt->generic.type = OMX_EVT_NONE;

    /* next event */
    evt++;
    if ((void *) evt >= ep->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE)
      evt = ep->unexp_eventq;
    ep->next_unexp_event = (void *) evt;
  }

  /* process expected events then */
  while (1) {
    volatile union omx_evt * evt = ep->next_exp_event;

    if (evt->generic.type == OMX_EVT_NONE)
      break;

    omx__process_event(ep, (union omx_evt *) evt);

    /* mark event as done */
    evt->generic.type = OMX_EVT_NONE;

    /* next event */
    evt++;
    if ((void *) evt >= ep->exp_eventq + OMX_EXP_EVENTQ_SIZE)
      evt = ep->exp_eventq;
    ep->next_exp_event = (void *) evt;
  }

  return OMX_SUCCESS;
}

omx_return_t
omx_register_unexp_handler(omx_endpoint_t ep,
			   omx_unexp_handler_t handler,
			   void *context)
{
  ep->unexp_handler = handler;
  ep->unexp_handler_context = context;

  return OMX_SUCCESS;
}

omx_return_t
omx_progress(omx_endpoint_t ep)
{
  return omx__progress(ep);
}

omx_return_t
omx_disable_progression(struct omx_endpoint *ep)
{
  if (ep->in_handler)
    return OMX_NOT_SUPPORTED_IN_HANDLER;

  ep->in_handler = 1;
  return OMX_SUCCESS;
}

omx_return_t
omx_reenable_progression(struct omx_endpoint *ep)
{
  ep->in_handler = 0;
  omx__progress(ep);
  return OMX_SUCCESS;
}
