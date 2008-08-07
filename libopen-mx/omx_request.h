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

#ifndef __omx_request_h__
#define __omx_request_h__

#include <stdlib.h>

#include "omx_lib.h"
#include "omx_list.h"

/*********************
 * Request allocation
 */

static inline union omx_request *
omx__request_alloc(struct omx_endpoint *ep)
{
  union omx_request * req;

#ifdef OMX_LIB_DEBUG
  req = calloc(1, sizeof(*req));
#else
  req = malloc(sizeof(*req));
#endif
  if (unlikely(!req))
    return NULL;

  req->generic.state = 0;
  req->generic.status.code = OMX_SUCCESS;

  return req;
}

static inline void
omx__request_free(struct omx_endpoint *ep, union omx_request * req)
{
  free(req);
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
omx__requeue_request(struct list_head *head,
		     union omx_request *req)
{
  list_add(&req->generic.queue_elt, head);
}

static inline void
omx___dequeue_request(union omx_request *req)
{
  list_del(&req->generic.queue_elt);
}

static inline void
omx__dequeue_request(struct list_head *head,
		     union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct list_head *e;
  list_for_each(e, head)
    if (req == list_entry(e, union omx_request, generic.queue_elt))
      goto found;

  omx__abort("Failed to find request in queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  omx___dequeue_request(req);
}

static inline union omx_request *
omx__first_request(struct list_head *head)
{
  return list_first_entry(head, union omx_request, generic.queue_elt);
}

static inline int
omx__empty_queue(struct list_head *head)
{
  return list_empty(head);
}

#define omx__foreach_request(head, req)		\
list_for_each_entry(req, head, generic.queue_elt)

#define omx__foreach_request_safe(head, req, next)	\
list_for_each_entry_safe(req, next, head, generic.queue_elt)

/********************************
 * Done request queue management
 */

/* mark the request as done while it is not done yet */
static inline void
omx__notify_request_done_early(struct omx_endpoint *ep, uint32_t ctxid,
			       union omx_request *req)
{
  if (unlikely(ep->zombies >= ep->zombie_max))
    return;

  omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_DONE));
  omx__debug_assert(req->generic.state);

  req->generic.state |= OMX_REQUEST_STATE_DONE;
  list_add_tail(&req->generic.done_anyctxid_elt, &ep->anyctxid.done_req_q);
  if (unlikely(HAS_CTXIDS(ep)))
    list_add_tail(&req->generic.done_ctxid_elt, &ep->ctxid[ctxid].done_req_q);

  /*
   * need to wakeup some possible send-done waiters (or recv-done for notify)
   * since this event does not come from the driver
   */
  omx__notify_user_event(ep);
}

static inline void
omx__notify_request_done(struct omx_endpoint *ep, uint32_t ctxid,
			 union omx_request *req)
{
  if (unlikely(req->generic.state & OMX_REQUEST_STATE_INTERNAL)) {
    /* no need to queue the request, just set the DONE status */
    omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_DONE));
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_ZOMBIE));

  } else if (likely(req->generic.state & OMX_REQUEST_STATE_ZOMBIE)) {
    /* request already completed by the application, just free it */
    omx__request_free(ep, req);
    ep->zombies--;

  } else if (unlikely(!(req->generic.state & OMX_REQUEST_STATE_DONE))) {
    /* queue the request to the done queue if not already done */
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    list_add_tail(&req->generic.done_anyctxid_elt, &ep->anyctxid.done_req_q);
    if (unlikely(HAS_CTXIDS(ep)))
      list_add_tail(&req->generic.done_ctxid_elt, &ep->ctxid[ctxid].done_req_q);
  }
}

static inline void
omx__dequeue_done_request(struct omx_endpoint *ep,
			  union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);
  struct list_head *e;

  list_for_each(e, &ep->anyctxid.done_req_q)
    if (req == list_entry(e, union omx_request, generic.done_anyctxid_elt))
      goto found2;
  omx__abort("Failed to find request in anyctxid queue for dequeueing\n");
 found2:

  if (unlikely(HAS_CTXIDS(ep))) {
    list_for_each(e, &ep->ctxid[ctxid].done_req_q)
      if (req == list_entry(e, union omx_request, generic.done_ctxid_elt))
	goto found;
    omx__abort("Failed to find request in ctxid queue for dequeueing\n");
  }
 found:

#endif /* OMX_LIB_DEBUG */
  list_del(&req->generic.done_anyctxid_elt);
  if (unlikely(HAS_CTXIDS(ep)))
    list_del(&req->generic.done_ctxid_elt);
}

#define omx__foreach_done_ctxid_request(ep, _ctxid, req)		\
list_for_each_entry(req, &ep->ctxid[_ctxid].done_req_q, generic.done_ctxid_elt)

#define omx__foreach_done_anyctxid_request(ep, req)		\
list_for_each_entry(req, &ep->anyctxid.done_req_q, generic.done_anyctxid_elt)

static inline union omx_request *
omx__first_done_anyctxid_request(struct omx_endpoint *ep)
{
  return list_first_entry(&ep->anyctxid.done_req_q, union omx_request, generic.done_anyctxid_elt);
}

static inline int
omx__empty_done_anyctxid_queue(struct omx_endpoint *ep)
{
  return list_empty(&ep->anyctxid.done_req_q);
}

/*********************************************
 * Partner non-acked request queue management
 */

static inline void
omx__enqueue_partner_non_acked_request(struct omx__partner *partner,
				       union omx_request *req)
{
  list_add_tail(&req->generic.partner_elt, &partner->non_acked_req_q);
}

static inline void
omx___dequeue_partner_non_acked_request(union omx_request *req)
{
  list_del(&req->generic.partner_elt);
}

#define omx__foreach_partner_non_acked_request(partner, req)	\
list_for_each_entry(req, &partner->non_acked_req_q, generic.partner_elt)

#define omx__foreach_partner_non_acked_request_safe(partner, req, next)		\
list_for_each_entry_safe(req, next, &partner->non_acked_req_q, generic.partner_elt)

/***************************************************
 * Partner throttling send request queue management
 */

static inline void
omx__enqueue_partner_throttling_request(struct omx__partner *partner,
					union omx_request *req)
{
  list_add_tail(&req->generic.partner_elt, &partner->throttling_send_req_q);
}

static inline void
omx___dequeue_partner_throttling_request(union omx_request *req)
{
  list_del(&req->generic.partner_elt);
}

static inline union omx_request *
omx__dequeue_first_partner_throttling_request(struct omx__partner *partner)
 {
  union omx_request *req;

  if (list_empty(&partner->throttling_send_req_q))
    return NULL;

  req = list_first_entry(&partner->throttling_send_req_q, union omx_request, generic.partner_elt);
  omx___dequeue_partner_throttling_request(req);
  return req;
}

static inline int
omx__empty_partner_throttling_request(struct omx__partner *partner)
{
  return list_empty(&partner->throttling_send_req_q);
}

#define omx__foreach_partner_throttling_request_safe(partner, req, next)		\
list_for_each_entry_safe(req, next, &partner->throttling_send_req_q, generic.partner_elt)

/***************************************************
 * Partner pending connect request queue management
 */

static inline void
omx__enqueue_partner_connect_request(struct omx__partner *partner,
				     union omx_request *req)
{
  list_add_tail(&req->generic.partner_elt, &partner->pending_connect_req_q);
}

static inline void
omx___dequeue_partner_connect_request(union omx_request *req)
{
  list_del(&req->generic.partner_elt);
}

static inline void
omx__dequeue_partner_connect_request(struct omx__partner *partner,
				     union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct list_head *e;
  list_for_each(e, &partner->pending_connect_req_q)
    if (req == list_entry(e, union omx_request, generic.partner_elt))
      goto found;

  omx__abort("Failed to find request in partner pending connect queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  omx___dequeue_partner_connect_request(req);
}

#define omx__foreach_partner_connect_request(partner, req)	\
list_for_each_entry(req, &partner->pending_connect_req_q, generic.partner_elt)

#define omx__foreach_partner_connect_request_safe(partner, req, next)	\
list_for_each_entry_safe(req, next, &partner->pending_connect_req_q, generic.partner_elt)

/*******************************************
 * Partner partial request queue management
 */

static inline void
omx__enqueue_partner_partial_request(struct omx__partner *partner,
				     union omx_request *req)
{
  list_add_tail(&req->generic.partner_elt, &partner->partial_recv_req_q);
}

static inline void
omx___dequeue_partner_partial_request(union omx_request *req)
{
  list_del(&req->generic.partner_elt);
}

static inline void
omx__dequeue_partner_partial_request(struct omx__partner *partner,
				     union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct list_head *e;
  list_for_each(e, &partner->partial_recv_req_q)
    if (req == list_entry(e, union omx_request, generic.partner_elt))
      goto found;

  omx__abort("Failed to find request in partner partial queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  omx___dequeue_partner_partial_request(req);
}

static inline union omx_request *
omx__first_partner_partial_request(struct omx__partner *partner)
{
  return list_first_entry(&partner->partial_recv_req_q, union omx_request, generic.partner_elt);
}

static inline int
omx__empty_partner_partial_queue(struct omx__partner *partner)
{
  return list_empty(&partner->partial_recv_req_q);
}

#define omx__foreach_partner_partial_request(partner, req)		\
list_for_each_entry(req, &partner->partial_recv_req_q, generic.partner_elt)

#define omx__foreach_partner_partial_request_safe(partner, req, next)		\
list_for_each_entry_safe(req, next, &partner->partial_recv_req_q, generic.partner_elt)

/*****************************************
 * Partner early packets queue management
 */

static inline void
omx___dequeue_partner_early_packet(struct omx__early_packet *early)
{
  list_del(&early->partner_elt);
}

static inline struct omx__early_packet *
omx__first_partner_early_packet(struct omx__partner *partner)
{
  return list_first_entry(&partner->early_recv_q, struct omx__early_packet, partner_elt);
}

static inline struct omx__early_packet *
omx__last_partner_early_packet(struct omx__partner *partner)
{
  return list_last_entry(&partner->early_recv_q, struct omx__early_packet, partner_elt);
}

static inline int
omx__empty_partner_early_packet_queue(struct omx__partner *partner)
{
  return list_empty(&partner->early_recv_q);
}

#define omx__foreach_partner_early_packet_safe(partner, early, next)	\
list_for_each_entry_safe(early, next, &partner->early_recv_q, partner_elt)

#define omx__foreach_partner_early_packet_reverse(partner, early)	\
list_for_each_entry_reverse(early, &partner->early_recv_q, partner_elt)

#endif /* __omx_request_h__ */
