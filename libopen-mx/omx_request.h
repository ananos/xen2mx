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

#ifndef __omx_request_h__
#define __omx_request_h__

#include <stdlib.h>

#include "omx_lib.h"
#include "omx_list.h"

/*********************
 * Request allocation
 */

static inline void
omx__request_alloc_init(struct omx_endpoint *ep)
{
#ifdef OMX_LIB_DEBUG
  ep->req_alloc_nr = 0;
#endif
}

static inline void
omx__request_alloc_exit(struct omx_endpoint *ep)
{
#ifdef OMX_LIB_DEBUG
  if (ep->req_alloc_nr)
    omx__verbose_printf(ep, "%d requests were not freed on endpoint close\n", ep->req_alloc_nr);
#endif
}

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

#ifdef OMX_LIB_DEBUG
  ep->req_alloc_nr++;
#endif
  return req;
}

static inline void
omx__request_free(struct omx_endpoint *ep, union omx_request * req)
{
  free(req);
#ifdef OMX_LIB_DEBUG
  ep->req_alloc_nr--;
#endif
}

extern void
omx__request_alloc_check(struct omx_endpoint *ep);

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

  omx__abort(NULL, "Failed to find request in queue for dequeueing\n");

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

static inline int
omx__queue_count(struct list_head *head)
{
  struct list_head *elt;
  int i=0;
  list_for_each(elt, head)
    i++;
  return i;
}

#define omx__foreach_request(head, req)		\
list_for_each_entry(req, head, generic.queue_elt)

#define omx__foreach_request_safe(head, req, next)	\
list_for_each_entry_safe(req, next, head, generic.queue_elt)

/*********************************
 * Request ctxid queue management
 */

static inline void
omx__enqueue_ctxid_request(struct list_head *head,
			   union omx_request *req)
{
  list_add_tail(&req->generic.ctxid_elt, head);
}

static inline void
omx___dequeue_ctxid_request(union omx_request *req)
{
  list_del(&req->generic.ctxid_elt);
}

static inline void
omx__dequeue_ctxid_request(struct list_head *head,
			   union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct list_head *e;
  list_for_each(e, head)
    if (req == list_entry(e, union omx_request, generic.ctxid_elt))
      goto found;

  omx__abort(NULL, "Failed to find request in ctxid queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  omx___dequeue_ctxid_request(req);
}

#define omx__foreach_ctxid_request(head, req)	\
list_for_each_entry(req, head, generic.ctxid_elt)

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

  omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_INTERNAL));
  omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_DONE));
  omx__debug_assert(req->generic.state);

  req->generic.state |= OMX_REQUEST_STATE_DONE;

  if (likely(!(req->generic.state & OMX_REQUEST_STATE_ZOMBIE))) {
    list_add_tail(&req->generic.done_elt, &ep->anyctxid.done_req_q);
    if (unlikely(HAS_CTXIDS(ep)))
      list_add_tail(&req->generic.ctxid_elt, &ep->ctxid[ctxid].done_req_q);
  }

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
#ifdef OMX_LIB_DEBUG
    omx__enqueue_request(&ep->internal_done_req_q, req);
#endif

  } else if (likely(req->generic.state & OMX_REQUEST_STATE_ZOMBIE)) {
    /* request already completed by the application, just free it */
    omx__request_free(ep, req);
    ep->zombies--;

  } else if (unlikely(!(req->generic.state & OMX_REQUEST_STATE_DONE))) {
    /* queue the request to the done queue */
    omx__debug_assert(!req->generic.state);
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    list_add_tail(&req->generic.done_elt, &ep->anyctxid.done_req_q);
    if (unlikely(HAS_CTXIDS(ep)))
      list_add_tail(&req->generic.ctxid_elt, &ep->ctxid[ctxid].done_req_q);
#ifdef OMX_LIB_DEBUG
    omx__enqueue_request(&ep->really_done_req_q, req);
#endif
  } else {
    /* request was marked as done early, its done_*_elt are already queued */
    omx__debug_assert(req->generic.state == OMX_REQUEST_STATE_DONE);
#ifdef OMX_LIB_DEBUG
    omx__enqueue_request(&ep->really_done_req_q, req);
#endif
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
    if (req == list_entry(e, union omx_request, generic.done_elt))
      goto found2;
  omx__abort(ep, "Failed to find request in anyctxid done queue for dequeueing\n");
 found2:

  if (unlikely(HAS_CTXIDS(ep))) {
    list_for_each(e, &ep->ctxid[ctxid].done_req_q)
      if (req == list_entry(e, union omx_request, generic.ctxid_elt))
	goto found;
    omx__abort(ep, "Failed to find request in ctxid done queue for dequeueing\n");
  }
 found:

  if (req->generic.state == OMX_REQUEST_STATE_DONE)
    omx__dequeue_request(&ep->really_done_req_q, req);
#endif /* OMX_LIB_DEBUG */
  list_del(&req->generic.done_elt);
  if (unlikely(HAS_CTXIDS(ep)))
    list_del(&req->generic.ctxid_elt);
}

#define omx__foreach_done_ctxid_request(ep, _ctxid, req)		\
list_for_each_entry(req, &ep->ctxid[_ctxid].done_req_q, generic.ctxid_elt)

#define omx__foreach_done_anyctxid_request(ep, req)		\
list_for_each_entry(req, &ep->anyctxid.done_req_q, generic.done_elt)

#define omx__foreach_done_anyctxid_request_safe(ep, req, next)		\
list_for_each_entry_safe(req, next, &ep->anyctxid.done_req_q, generic.done_elt)

static inline union omx_request *
omx__first_done_anyctxid_request(struct omx_endpoint *ep)
{
  return list_first_entry(&ep->anyctxid.done_req_q, union omx_request, generic.done_elt);
}

static inline int
omx__empty_done_ctxid_queue(struct omx_endpoint *ep, uint32_t ctxid)
{
  return list_empty(&ep->ctxid[ctxid].done_req_q);
}

static inline int
omx__empty_done_anyctxid_queue(struct omx_endpoint *ep)
{
  return list_empty(&ep->anyctxid.done_req_q);
}

/****************************
 * Partner queues management
 */

static inline void
omx__enqueue_partner_request(struct list_head *head,
			     union omx_request *req)
{
  list_add_tail(&req->generic.partner_elt, head);
}

static inline void
omx___dequeue_partner_request(union omx_request *req)
{
  list_del(&req->generic.partner_elt);
}

static inline void
omx__dequeue_partner_request(struct list_head *head,
			     union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct list_head *e;
  list_for_each(e, head)
    if (req == list_entry(e, union omx_request, generic.partner_elt))
      goto found;

  omx__abort(NULL, "Failed to find request in partner queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  omx___dequeue_partner_request(req);
}

static inline int
omx__empty_partner_queue(struct list_head *head)
{
  return list_empty(head);
}

static inline union omx_request *
omx__first_partner_request(struct list_head *head)
{
  return list_first_entry(head, union omx_request, generic.partner_elt);
}

static inline union omx_request *
omx__dequeue_first_partner_request(struct list_head *head)
 {
  union omx_request *req;

  if (list_empty(head))
    return NULL;

  req = list_first_entry(head, union omx_request, generic.partner_elt);
  omx___dequeue_partner_request(req);
  return req;
}

#define omx__foreach_partner_request(head, req)	\
list_for_each_entry(req, head, generic.partner_elt)

#define omx__foreach_partner_request_safe(head, req, next)	\
list_for_each_entry_safe(req, next, head, generic.partner_elt)

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

#define omx__foreach_partner_early_packet(partner, early)	\
list_for_each_entry(early, &partner->early_recv_q, partner_elt)

#define omx__foreach_partner_early_packet_safe(partner, early, next)	\
list_for_each_entry_safe(early, next, &partner->early_recv_q, partner_elt)

#define omx__foreach_partner_early_packet_reverse(partner, early)	\
list_for_each_entry_reverse(early, &partner->early_recv_q, partner_elt)

#endif /* __omx_request_h__ */
