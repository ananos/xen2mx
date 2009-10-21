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

#include <stdint.h>
#include <sys/ioctl.h>

#include "omx_lib.h"
#include "omx_request.h"

/* per endpoint queue of sleepers */
struct omx__sleeper {
  struct list_head list_elt;
  int need_wakeup;
};

/**************************
 * Common sleeping routine
 */

static omx_return_t
omx__wait(struct omx_endpoint *ep,
	  struct omx_cmd_wait_event *wait_param,
	  uint32_t ms_timeout,
	  const char *caller)
{
  int err;

  if (omx__driver_desc->jiffies >= wait_param->jiffies_expire
      || wait_param->status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT
      || wait_param->status == OMX_CMD_WAIT_EVENT_STATUS_WAKEUP
      || (omx__globals.waitintr && wait_param->status == OMX_CMD_WAIT_EVENT_STATUS_INTR))
    /* this is not an error in most cases, let the caller handle it if needed */
    return OMX_TIMEOUT;

  if (ms_timeout == OMX_TIMEOUT_INFINITE)
    omx__debug_printf(WAIT, ep, "%s going to sleep at %lld for ever\n",
		      caller, (unsigned long long) omx__driver_desc->jiffies);
  else
    omx__debug_printf(WAIT, ep, "%s going to sleep at %lld until %lld\n",
		      caller,
		      (unsigned long long) omx__driver_desc->jiffies,
		      (unsigned long long) wait_param->jiffies_expire);

  wait_param->next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
  wait_param->next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
  wait_param->user_event_index = ep->desc->user_event_index;
  omx__prepare_progress_wakeup(ep);

  /* release the lock while sleeping */
  OMX__ENDPOINT_UNLOCK(ep);
  err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, wait_param);
  OMX__ENDPOINT_LOCK(ep);

  OMX_VALGRIND_MEMORY_MAKE_READABLE(wait_param, sizeof(*wait_param));

#ifdef OMX_LIB_DEBUG
  {
    uint64_t now = omx__driver_desc->jiffies;
    if (ms_timeout != OMX_TIMEOUT_INFINITE && now > wait_param->jiffies_expire + 2) {
      /* tolerate 2 jiffies of timeshift */
      omx__verbose_printf(ep, "Sleep for %ld ms actually slept until jiffies %lld instead of %lld\n",
			  (unsigned long) ms_timeout,
			  (unsigned long long) now,
			  (unsigned long long) wait_param->jiffies_expire);
    }
  }
#endif

  omx__debug_printf(WAIT, ep, "%s woken up at %lld\n",
		    caller,
		    (unsigned long long) omx__driver_desc->jiffies);

  if (unlikely(err < 0))
    omx__ioctl_errno_to_return_checked(OMX_SUCCESS,
				       "wait event in the driver");

  omx__debug_printf(WAIT, ep, "%s wait event result %d\n",
		    caller,
		    wait_param->status);

  return OMX_SUCCESS;
}

/*********************************************
 * Test/Wait a single request and complete it
 */

static INLINE void
omx__test_success(struct omx_endpoint *ep, union omx_request *req,
		  struct omx_status *status)
{
  memcpy(status, &req->generic.status, sizeof(*status));
  omx__dequeue_done_request(ep, req);

  if (likely(req->generic.state != OMX_REQUEST_STATE_DONE)) {
    /* the request is not actually done, zombify it */
    req->generic.state &= ~OMX_REQUEST_STATE_DONE;
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
    ep->zombies++;
  } else {
    /* the request is done for real, delete it */
    omx__request_free(ep, req);
  }
}

static INLINE uint32_t
omx__test_common(struct omx_endpoint *ep, union omx_request **requestp,
		 struct omx_status *status)
{
  union omx_request * req = *requestp;

  if (likely(req->generic.state & OMX_REQUEST_STATE_DONE)) {
    omx__test_success(ep, req, status);
    *requestp = NULL;
    return 1;
  } else {
    return 0;
  }
}

/* API omx_test */
omx_return_t
omx_test(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t *resultp)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  result = omx__test_common(ep, requestp, status);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  *resultp = result;
  return ret;
}

/* API omx_wait */
omx_return_t
omx_wait(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t *resultp,
	 uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  struct omx__sleeper sleeper;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  OMX__ENDPOINT_LOCK(ep);
  sleeper.need_wakeup = 0;
  list_add_tail(&sleeper.list_elt, &ep->sleepers);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (!sleeper.need_wakeup) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if ((result = omx__test_common(ep, requestp, status)) != 0)
	goto out_with_lock;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }

    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;
  wait_param.status = OMX_CMD_WAIT_EVENT_STATUS_EVENT;

  while (1) {
    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if ((result = omx__test_common(ep, requestp, status)) != 0)
      goto out_with_lock;

    ret = omx__wait(ep, &wait_param, ms_timeout, "wait");
    if (ret != OMX_SUCCESS) {
      if (ret == OMX_TIMEOUT)
	ret = OMX_SUCCESS;
      goto out_with_lock;
    }
  }

 out_with_lock:
  list_del(&sleeper.list_elt);
  OMX__ENDPOINT_UNLOCK(ep);
  *resultp = result;
  return ret;
}

/* mark a request as zombie, assuming it wasn't already */
void
omx__forget(struct omx_endpoint *ep, union omx_request *req)
{
  if (req->generic.state == OMX_REQUEST_STATE_DONE) {
    /* just dequeue and free */
    omx__dequeue_done_request(ep, req);
    omx__request_free(ep, req);
  } else {
    /* mark as zombie and let the real completion delete it later */
    if (req->generic.state & OMX_REQUEST_STATE_DONE) {
      /* remove from the done queue since the application doesn't want any completion */
      req->generic.state &= ~OMX_REQUEST_STATE_DONE;
      omx__dequeue_done_request(ep, req);
    }
    req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
    ep->zombies++;
  }
}

/* API omx_forget */
omx_return_t
omx_forget(struct omx_endpoint *ep, union omx_request **requestp)
{
  union omx_request * req = *requestp;

  OMX__ENDPOINT_LOCK(ep);

  if (!(req->generic.state & OMX_REQUEST_STATE_ZOMBIE))
    omx__forget(ep, req);

  OMX__ENDPOINT_UNLOCK(ep);

  *requestp = NULL;

  return OMX_SUCCESS;
}

/***********************************************
 * Test/Wait any single request and complete it
 */

static INLINE uint32_t
omx__test_any_common(struct omx_endpoint *ep,
		     uint64_t match_info, uint64_t match_mask,
		     omx_status_t *status)
{
  union omx_request * req;

  if (likely(!HAS_CTXIDS(ep) || MATCHING_CROSS_CTXIDS(ep, match_mask))) {
    /* no ctxids, or matching across multiple ctxids, so use the anyctxid queue */
    omx__foreach_done_anyctxid_request(ep, req) {
      if (likely((req->generic.status.match_info & match_mask) == match_info)) {
	omx__test_success(ep, req, status);
	return 1;
      }
    }

  } else {
    /* use one of the ctxid queues */
    uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
    omx__foreach_done_ctxid_request(ep, ctxid, req) {
      if (likely((req->generic.status.match_info & match_mask) == match_info)) {
	omx__test_success(ep, req, status);
	return 1;
      }
    }
  }

  return 0;
}

/* API omx_test_any */
omx_return_t
omx_test_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *resultp)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  if (unlikely(match_info & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCH_MASK,
			     "test_any with match info %llx mask %llx",
			     (unsigned long long) match_info, (unsigned long long) match_mask);
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  result = omx__test_any_common(ep, match_info, match_mask, status);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  *resultp = result;
  return ret;
}

/* API omx_wait_any */
omx_return_t
omx_wait_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *resultp,
	     uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  struct omx__sleeper sleeper;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  if (unlikely(match_info & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCH_MASK,
			     "wait_any with match info %llx mask %llx",
			     (unsigned long long) match_info, (unsigned long long) match_mask);
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);
  sleeper.need_wakeup = 0;
  list_add_tail(&sleeper.list_elt, &ep->sleepers);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (!sleeper.need_wakeup) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if ((result = omx__test_any_common(ep, match_info, match_mask, status)) != 0)
	goto out_with_lock;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }

    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;
  wait_param.status = OMX_CMD_WAIT_EVENT_STATUS_EVENT;

  while (1) {
    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if ((result = omx__test_any_common(ep, match_info, match_mask, status)) != 0)
      goto out_with_lock;

    ret = omx__wait(ep, &wait_param, ms_timeout, "wait_any");
    if (ret != OMX_SUCCESS) {
      if (ret == OMX_TIMEOUT)
	ret = OMX_SUCCESS;
      goto out_with_lock;
    }
  }

 out_with_lock:
  list_del(&sleeper.list_elt);
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  *resultp = result;
  return ret;
}

/*****************************************************
 * Test/Wait any single request without completing it
 */

static INLINE uint32_t
omx__ipeek_common(const struct omx_endpoint *ep, union omx_request **requestp)
{
  if (unlikely(omx__empty_done_anyctxid_queue(ep))) {
    return 0;
  } else {
    *requestp = omx__first_done_anyctxid_request(ep);
    return 1;
  }
}

/* API omx_ipeek */
omx_return_t
omx_ipeek(struct omx_endpoint *ep, union omx_request **requestp,
	  uint32_t *resultp)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  result = omx__ipeek_common(ep, requestp);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  *resultp = result;
  return ret;
}

/* API omx_peek */
omx_return_t
omx_peek(struct omx_endpoint *ep, union omx_request **requestp,
	 uint32_t *resultp, uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  struct omx__sleeper sleeper;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  OMX__ENDPOINT_LOCK(ep);
  sleeper.need_wakeup = 0;
  list_add_tail(&sleeper.list_elt, &ep->sleepers);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (!sleeper.need_wakeup) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if ((result = omx__ipeek_common(ep, requestp)) != 0)
	goto out_with_lock;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }

    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;
  wait_param.status = OMX_CMD_WAIT_EVENT_STATUS_EVENT;

  while (1) {
    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if ((result = omx__ipeek_common(ep, requestp)) != 0)
      goto out_with_lock;

    ret = omx__wait(ep, &wait_param, ms_timeout, "peek");
    if (ret != OMX_SUCCESS) {
      if (ret == OMX_TIMEOUT)
	ret = OMX_SUCCESS;
      goto out_with_lock;
    }
  }

 out_with_lock:
  list_del(&sleeper.list_elt);
  OMX__ENDPOINT_UNLOCK(ep);
  *resultp = result;
  return ret;
}

/****************************************
 * Test/Wait on a request being buffered
 */

/* API omx_ibuffered */
omx_return_t
omx_ibuffered(struct omx_endpoint *ep, union omx_request **requestp,
	      uint32_t *resultp)
{
  union omx_request *req = *requestp;
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  switch (req->generic.type) {
  case OMX_REQUEST_TYPE_SEND_TINY:
    /* Tiny are buffered as soon as they pass the throttling check
     * (no NEED_SEQNUM state)
     */
  case OMX_REQUEST_TYPE_SEND_SMALL:
    /* Small are buffered when they pass the throttling check
     * (no NEED_SEQNUM state)
     * since the copy buffer is allocated when allocating the request.
     */
  case OMX_REQUEST_TYPE_SEND_MEDIUMSQ:
    /* Mediumsq are buffered in the sendq once they got all their resources allocated
     * (no NEED_RESOURCES state)
     * and they passed the throttling check (no NEED_SEQNUM)
     */

    if (!(req->generic.state & (OMX_REQUEST_STATE_NEED_RESOURCES | OMX_REQUEST_STATE_NEED_SEQNUM)))
      result = 1;
    break;

  case OMX_REQUEST_TYPE_SEND_SELF:
    /* communications to self are immediately copied to the receive side */
    result = 1;
    break;

  case OMX_REQUEST_TYPE_SEND_MEDIUMVA:
    /* Medium send from vaddr is never buffered */
  case OMX_REQUEST_TYPE_SEND_LARGE:
    /* Large send is never buffered */
    break;

  default:
    /* All non-send requests cannot be buffered */
    ret = OMX_BAD_REQUEST;
  }

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  *resultp = result;
  return ret;
}

/********************************
 * Test/Wait unexpected messages
 */

static INLINE uint32_t
omx__iprobe_common(const struct omx_endpoint *ep,
		   uint64_t match_info, uint64_t match_mask,
		   omx_status_t *status)
{
  union omx_request * req;

  if (likely(!HAS_CTXIDS(ep) || MATCHING_CROSS_CTXIDS(ep, match_mask))) {
    /* no ctxids, or matching across multiple ctxids, so use the anyctxid queue */
    omx__foreach_request(&ep->anyctxid.unexp_req_q, req) {
      if (likely((req->generic.status.match_info & match_mask) == match_info)) {
	memcpy(status, &req->generic.status, sizeof(*status));
	return 1;
      }
    }

  } else {
    /* use one of the ctxid queues */
    uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
    omx__foreach_ctxid_request(&ep->ctxid[ctxid].unexp_req_q, req) {
      if (likely((req->generic.status.match_info & match_mask) == match_info)) {
	memcpy(status, &req->generic.status, sizeof(*status));
	return 1;
      }
    }
  }

  return 0;
}

/* API omx_iprobe */
omx_return_t
omx_iprobe(struct omx_endpoint *ep, uint64_t match_info, uint64_t match_mask,
	   omx_status_t *status, uint32_t *resultp)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  if (unlikely(match_info & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCH_MASK,
			     "iprobe with match info %llx mask %llx",
			     (unsigned long long) match_info, (unsigned long long) match_mask);
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  result = omx__iprobe_common(ep, match_info, match_mask, status);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  *resultp = result;
  return ret;
}

/* API omx_probe */
omx_return_t
omx_probe(struct omx_endpoint *ep,
	  uint64_t match_info, uint64_t match_mask,
	  omx_status_t *status, uint32_t *resultp,
	  uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  struct omx__sleeper sleeper;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;
  uint32_t result = 0;

  if (unlikely(match_info & ~match_mask)) {
    ret = omx__error_with_ep(ep, OMX_BAD_MATCH_MASK,
			     "probe with match info %llx mask %llx",
			     (unsigned long long) match_info, (unsigned long long) match_mask);
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);
  sleeper.need_wakeup = 0;
  list_add_tail(&sleeper.list_elt, &ep->sleepers);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (!sleeper.need_wakeup) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if ((result = omx__iprobe_common(ep, match_info, match_mask, status)) != 0)
	goto out_with_lock;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }

    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;
  wait_param.status = OMX_CMD_WAIT_EVENT_STATUS_EVENT;

  while (1) {
    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if ((result = omx__iprobe_common(ep, match_info, match_mask, status)) != 0)
      goto out_with_lock;

    ret = omx__wait(ep, &wait_param, ms_timeout, "probe");
    if (ret != OMX_SUCCESS) {
      if (ret == OMX_TIMEOUT)
	ret = OMX_SUCCESS;
      goto out_with_lock;
    }
  }

 out_with_lock:
  list_del(&sleeper.list_elt);
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  *resultp = result;
  return ret;
}

/************************************
 * Synchronous connect specific wait
 */

/* called with the endpoint lock held */
omx_return_t
omx__connect_wait(omx_endpoint_t ep, union omx_request * req, uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  struct omx__sleeper sleeper;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;

  sleeper.need_wakeup = 0;
  list_add_tail(&sleeper.list_elt, &ep->sleepers);

  if (omx__globals.connect_pollall) {
    /* busy spin and poll other endpoints instead of sleeping */
    while (!sleeper.need_wakeup) {
      OMX__ENDPOINT_UNLOCK(ep);
      omx__foreach_endpoint((void *) omx_progress, NULL);
      OMX__ENDPOINT_LOCK(ep);
    
      if (req->generic.state == (OMX_REQUEST_STATE_DONE|OMX_REQUEST_STATE_INTERNAL))
	goto out;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire) {
	/* let the caller handle errors */
	ret = OMX_TIMEOUT;
	goto out;
      }
    }

    /* let the caller handle errors */
    ret = OMX_TIMEOUT;
    goto out;
  }

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (!sleeper.need_wakeup) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out;

      if (req->generic.state == (OMX_REQUEST_STATE_DONE|OMX_REQUEST_STATE_INTERNAL))
	goto out;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire) {
	/* let the caller handle errors */
	ret = OMX_TIMEOUT;
	goto out;
      }

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }

    /* let the caller handle errors */
    ret = OMX_TIMEOUT;
    goto out;
  }

  wait_param.jiffies_expire = jiffies_expire;
  wait_param.status = OMX_CMD_WAIT_EVENT_STATUS_EVENT;

  while (1) {
    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out;

    if (req->generic.state == (OMX_REQUEST_STATE_DONE|OMX_REQUEST_STATE_INTERNAL))
      goto out;

    ret = omx__wait(ep, &wait_param, ms_timeout, "connect");
    if (ret != OMX_SUCCESS) {
      /* keep OMX_TIMEOUT as is and let the caller handle errors */
      goto out;
    }
  }

 out:
  list_del(&sleeper.list_elt);
  return ret;
}

/*****************
 * Wakeup waiters
 */

static INLINE omx_return_t
omx__wakeup(struct omx_endpoint *ep, uint32_t status)
{
  if (omx__globals.waitspin) {
    /* change waitspiner's wakeup status */
    struct omx__sleeper *sleeper;
    list_for_each_entry(sleeper, &ep->sleepers, list_elt)
      sleeper->need_wakeup = 1;

  } else if (!list_empty(&ep->sleepers)) {
    /* enter the driver to wakeup sleeper if any */
    struct omx_cmd_wakeup wakeup;
    int err;

    wakeup.status = status;

    err = ioctl(ep->fd, OMX_CMD_WAKEUP, &wakeup);
    if (unlikely(err < 0))
      omx__ioctl_errno_to_return_checked(OMX_SUCCESS,
					 "wakeup sleepers in the driver");
  }

  return OMX_SUCCESS;
}

/* add a user-generated event and wakeup in-driver sleepers if necessary */
void
omx__notify_user_event(struct omx_endpoint *ep)
{
  ep->desc->user_event_index++;

  omx__wakeup(ep, OMX_CMD_WAIT_EVENT_STATUS_EVENT);
}

/* API omx_wakeup */
omx_return_t
omx_wakeup(struct omx_endpoint *ep)
{
  omx_return_t ret  = OMX_SUCCESS;
  OMX__ENDPOINT_LOCK(ep);
  ret = omx__wakeup(ep, OMX_CMD_WAIT_EVENT_STATUS_WAKEUP);
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}
