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
#include <sys/ioctl.h>

#include "omx_lib.h"
#include "omx_request.h"

static inline void
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

/* API omx_forget */
omx_return_t
omx_forget(struct omx_endpoint *ep, union omx_request **requestp)
{
  union omx_request * req = *requestp;

  OMX__ENDPOINT_LOCK(ep);

  if (!(req->generic.state & OMX_REQUEST_STATE_ZOMBIE)) {
    if (req->generic.state == OMX_REQUEST_STATE_DONE) {
      /* want to forget a request that is ready to complete? just complete it and ignore the return value */
      struct omx_status dummy;
      omx__test_success(ep, req, &dummy);
    } else {
      /* mark as zombie and let the real completion delete it later */
      req->generic.state |= OMX_REQUEST_STATE_ZOMBIE;
      ep->zombies++;
    }
  }

  OMX__ENDPOINT_UNLOCK(ep);

  *requestp = NULL;

  return OMX_SUCCESS;
}

static inline uint32_t
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
	 struct omx_status *status, uint32_t * result)
{
  omx_return_t ret = OMX_SUCCESS;

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  *result = omx__test_common(ep, requestp, status);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/* API omx_wait */
omx_return_t
omx_wait(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result,
	 uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;

  OMX__ENDPOINT_LOCK(ep);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (1) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if (omx__test_common(ep, requestp, status)) {
	*result = 1;
	goto out_with_lock;
      }

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }
  }

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  if (omx__test_common(ep, requestp, status)) {
    *result = 1;
    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;

  while (1) {
    int err;

    if (ms_timeout == OMX_TIMEOUT_INFINITE)
      omx__debug_printf(WAIT, "omx_wait going to sleep at %lld for ever\n",
			(unsigned long long) omx__driver_desc->jiffies);
    else
      omx__debug_printf(WAIT, "omx_wait going to sleep at %lld until %lld\n",
			(unsigned long long) omx__driver_desc->jiffies,
			(unsigned long long) wait_param.jiffies_expire);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    omx__prepare_progress_wakeup(ep);

    /* release the lock while sleeping */
    OMX__ENDPOINT_UNLOCK(ep);
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);
    OMX__ENDPOINT_LOCK(ep);

    OMX_VALGRIND_MEMORY_MAKE_READABLE(&wait_param, sizeof(wait_param));

    omx__debug_printf(WAIT, "omx_wait woken up at %lld\n",
		      (unsigned long long) omx__driver_desc->jiffies);

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_wait wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if (omx__test_common(ep, requestp, status)) {
      *result = 1;
      goto out_with_lock;
    }

    if (omx__driver_desc->jiffies >= wait_param.jiffies_expire
	|| wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT
	|| (omx__globals.waitintr && wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR)) {
      *result = 0;
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_wait going back to sleep\n");
  }

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

static inline uint32_t
omx__test_any_common(struct omx_endpoint *ep,
		     uint64_t match_info, uint64_t match_mask,
		     omx_status_t *status)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  union omx_request * req;

  omx__foreach_done_request(ep, ctxid, req) {
    if (likely((req->generic.status.match_info & match_mask) == match_info)) {
      omx__test_success(ep, req, status);
      return 1;
    }
  }

  return 0;
}

/* API omx_test_any */
omx_return_t
omx_test_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(match_info & ~match_mask)) {
    ret = OMX_BAD_MATCH_MASK;
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(!CHECK_MATCHING_WITH_CTXID(ep, match_mask))) {
    ret = OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  *result = omx__test_any_common(ep, match_info, match_mask, status);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  return ret;
}

/* API omx_wait_any */
omx_return_t
omx_wait_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result,
	     uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(match_info & ~match_mask)) {
    ret = OMX_BAD_MATCH_MASK;
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(!CHECK_MATCHING_WITH_CTXID(ep, match_mask))) {
    ret = OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (1) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if (omx__test_any_common(ep, match_info, match_mask, status)) {
	*result = 1;
	goto out_with_lock;
      }

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }
  }

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  if (omx__test_any_common(ep, match_info, match_mask, status)) {
    *result = 1;
    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;

  while (1) {
    int err;

    if (ms_timeout == OMX_TIMEOUT_INFINITE)
      omx__debug_printf(WAIT, "omx_wait_any going to sleep at %lld for ever\n",
			(unsigned long long) omx__driver_desc->jiffies);
    else
      omx__debug_printf(WAIT, "omx_wait_any going to sleep at %lld until %lld\n",
			(unsigned long long) omx__driver_desc->jiffies,
			(unsigned long long) wait_param.jiffies_expire);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    omx__prepare_progress_wakeup(ep);

    /* release the lock while sleeping */
    OMX__ENDPOINT_UNLOCK(ep);
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);
    OMX__ENDPOINT_LOCK(ep);

    OMX_VALGRIND_MEMORY_MAKE_READABLE(&wait_param, sizeof(wait_param));

    omx__debug_printf(WAIT, "omx_wait_any woken up at %lld\n",
		      (unsigned long long) omx__driver_desc->jiffies);

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_wait_any wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if (omx__test_any_common(ep, match_info, match_mask, status)) {
      *result = 1;
      goto out_with_lock;
    }

    if (omx__driver_desc->jiffies >= wait_param.jiffies_expire
	|| wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT
	|| (omx__globals.waitintr && wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR)) {
      *result = 0;
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_wait going back to sleep\n");
  }

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  return ret;
}

static inline uint32_t
omx__ipeek_common(struct omx_endpoint *ep, union omx_request **requestp)
{
  if (unlikely(omx__empty_done_queue(ep, 0))) {
    return 0;
  } else {
    *requestp = omx__first_done_request(ep, 0);
    return 1;
  }
}

/* API omx_ipeek */
omx_return_t
omx_ipeek(struct omx_endpoint *ep, union omx_request **requestp,
	  uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(ep->ctxid_bits)) {
    ret = OMX_NOT_SUPPORTED_WITH_CONTEXT_ID;
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  *result = omx__ipeek_common(ep, requestp);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  return ret;
}

/* API omx_peek */
omx_return_t
omx_peek(struct omx_endpoint *ep, union omx_request **requestp,
	 uint32_t *result, uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(ep->ctxid_bits)) {
    ret = OMX_NOT_SUPPORTED_WITH_CONTEXT_ID;
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (1) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if (omx__ipeek_common(ep, requestp)) {
	*result = 1;
	goto out_with_lock;
      }

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }
  }

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  if (omx__ipeek_common(ep, requestp)) {
    *result = 1;
    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;

  while (1) {
    int err;

    if (ms_timeout == OMX_TIMEOUT_INFINITE)
      omx__debug_printf(WAIT, "omx_peek going to sleep at %lld for ever\n",
			(unsigned long long) omx__driver_desc->jiffies);
    else
      omx__debug_printf(WAIT, "omx_peek going to sleep at %lld until %lld\n",
			(unsigned long long) omx__driver_desc->jiffies,
			(unsigned long long) wait_param.jiffies_expire);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    omx__prepare_progress_wakeup(ep);

    /* release the lock while sleeping */
    OMX__ENDPOINT_UNLOCK(ep);
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);
    OMX__ENDPOINT_LOCK(ep);

    OMX_VALGRIND_MEMORY_MAKE_READABLE(&wait_param, sizeof(wait_param));

    omx__debug_printf(WAIT, "omx_peek woken up at %lld\n",
		      (unsigned long long) omx__driver_desc->jiffies);

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_peek wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if (omx__ipeek_common(ep, requestp)) {
      *result = 1;
      goto out_with_lock;
    }

    if (omx__driver_desc->jiffies >= wait_param.jiffies_expire
	|| wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT
	|| (omx__globals.waitintr && wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR)) {
      *result = 0;
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_wait going back to sleep\n");
  }

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  return ret;
}

static inline uint32_t
omx__iprobe_common(struct omx_endpoint *ep,
		   uint64_t match_info, uint64_t match_mask,
		   omx_status_t *status)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  union omx_request * req;

  omx__foreach_request(&ep->ctxid[ctxid].unexp_req_q, req) {
    if (likely((req->generic.status.match_info & match_mask) == match_info)) {
      memcpy(status, &req->generic.status, sizeof(*status));
      return 1;
    }
  }

  return 0;
}

/* API omx_iprobe */
omx_return_t
omx_iprobe(struct omx_endpoint *ep, uint64_t match_info, uint64_t match_mask,
	   omx_status_t *status, uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(match_info & ~match_mask)) {
    ret = OMX_BAD_MATCH_MASK;
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(!CHECK_MATCHING_WITH_CTXID(ep, match_mask))) {
    ret = OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  *result = omx__iprobe_common(ep, match_info, match_mask, status);

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  return ret;
}

/* API omx_probe */
omx_return_t
omx_probe(struct omx_endpoint *ep,
	  uint64_t match_info, uint64_t match_mask,
	  omx_status_t *status, uint32_t *result,
	  uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(match_info & ~match_mask)) {
    ret = OMX_BAD_MATCH_MASK;
    goto out;
  }

  /* check that there's no wildcard in the context id range */
  if (unlikely(!CHECK_MATCHING_WITH_CTXID(ep, match_mask))) {
    ret = OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
    goto out;
  }

  OMX__ENDPOINT_LOCK(ep);

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (1) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	goto out_with_lock;

      if (omx__iprobe_common(ep, match_info, match_mask, status)) {
	*result = 1;
	goto out_with_lock;
      }

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	goto out_with_lock;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }
  }

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out_with_lock;

  if (omx__iprobe_common(ep, match_info, match_mask, status)) {
    *result = 1;
    goto out_with_lock;
  }

  wait_param.jiffies_expire = jiffies_expire;

  while (1) {
    int err;

    if (ms_timeout == OMX_TIMEOUT_INFINITE)
      omx__debug_printf(WAIT, "omx_probe going to sleep at %lld for ever\n",
			(unsigned long long) omx__driver_desc->jiffies);
    else
      omx__debug_printf(WAIT, "omx_probe going to sleep at %lld until %lld\n",
			(unsigned long long) omx__driver_desc->jiffies,
			(unsigned long long) wait_param.jiffies_expire);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    omx__prepare_progress_wakeup(ep);

    /* release the lock while sleeping */
    OMX__ENDPOINT_UNLOCK(ep);
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);
    OMX__ENDPOINT_LOCK(ep);

    OMX_VALGRIND_MEMORY_MAKE_READABLE(&wait_param, sizeof(wait_param));

    omx__debug_printf(WAIT, "omx_probe woken up at %lld\n",
		      (unsigned long long) omx__driver_desc->jiffies);

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_probe wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out_with_lock;

    if (omx__iprobe_common(ep, match_info, match_mask, status)) {
      *result = 1;
      goto out_with_lock;
    }

    if (omx__driver_desc->jiffies >= wait_param.jiffies_expire
	|| wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT
	|| (omx__globals.waitintr && wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR)) {
      *result = 0;
      goto out_with_lock;
    }

    omx__debug_printf(WAIT, "omx_wait going back to sleep\n");
  }

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
 out:
  return ret;
}

/* called with the endpoint lock held */
omx_return_t
omx__connect_wait(omx_endpoint_t ep, union omx_request * req, uint32_t ms_timeout)
{
  struct omx_cmd_wait_event wait_param;
  uint64_t jiffies_expire = omx__timeout_ms_to_absolute_jiffies(ms_timeout);
  omx_return_t ret;

  if (omx__globals.waitspin) {
    /* busy spin instead of sleeping */
    while (1) {
      ret = omx__progress(ep);
      if (unlikely(ret != OMX_SUCCESS))
	return ret;

      if (req->generic.state == (OMX_REQUEST_STATE_DONE|OMX_REQUEST_STATE_INTERNAL))
	return OMX_SUCCESS;

      if (ms_timeout != OMX_TIMEOUT_INFINITE && omx__driver_desc->jiffies >= jiffies_expire)
	return OMX_CONNECTION_TIMEOUT;

      /* release the lock a bit */
      OMX__ENDPOINT_UNLOCK(ep);
      OMX__ENDPOINT_LOCK(ep);
    }
  }

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    /* request is queued, do not try to free it */
    return ret;

  if (req->generic.state == (OMX_REQUEST_STATE_DONE|OMX_REQUEST_STATE_INTERNAL))
    return OMX_SUCCESS;

  wait_param.jiffies_expire = jiffies_expire;

  while (1) {
    int err;

    if (ms_timeout == OMX_TIMEOUT_INFINITE)
      omx__debug_printf(WAIT, "omx_connect going to sleep at %lld for ever\n",
			(unsigned long long) omx__driver_desc->jiffies);
    else
      omx__debug_printf(WAIT, "omx_connect going to sleep at %lld until %lld\n",
			(unsigned long long) omx__driver_desc->jiffies,
			(unsigned long long) wait_param.jiffies_expire);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    omx__prepare_progress_wakeup(ep);

    /* release the lock while sleeping */
    OMX__ENDPOINT_UNLOCK(ep);
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);
    OMX__ENDPOINT_LOCK(ep);

    OMX_VALGRIND_MEMORY_MAKE_READABLE(&wait_param, sizeof(wait_param));

    omx__debug_printf(WAIT, "omx_connect woken up at %lld\n",
		      (unsigned long long) omx__driver_desc->jiffies);

    if (unlikely(err < 0))
      return omx__errno_to_return("ioctl WAIT_EVENT");

    omx__debug_printf(WAIT, "omx_connect wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      return ret;

    if (req->generic.state == (OMX_REQUEST_STATE_DONE|OMX_REQUEST_STATE_INTERNAL))
      return OMX_SUCCESS;

    if (omx__driver_desc->jiffies >= wait_param.jiffies_expire
	|| wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT
	|| (omx__globals.waitintr && wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR))
      return OMX_CONNECTION_TIMEOUT;

    omx__debug_printf(WAIT, "omx_connect going back to sleep\n");
  }

  return OMX_SUCCESS;
}
