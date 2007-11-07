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
#include <sys/ioctl.h>

#include "omx_lib.h"
#include "omx_request.h"

static inline uint32_t
omx__test_common(struct omx_endpoint *ep, union omx_request **requestp,
		 struct omx_status *status)
{
  union omx_request * req = *requestp;

  if (unlikely(req->generic.state)) {
    return 0;
  } else {
    uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);
    omx__dequeue_request(&ep->ctxid[ctxid].done_req_q, req);
    memcpy(status, &req->generic.status, sizeof(*status));
    omx__request_free(req);
    *requestp = NULL;
    return 1;
  }
}

omx_return_t
omx_test(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result)
{
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  *result = omx__test_common(ep, requestp, status);

 out:
  return ret;
}

omx_return_t
omx_wait(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result,
	 uint32_t timeout)
{
  struct omx_cmd_wait_event wait_param;
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  if (omx__test_common(ep, requestp, status)) {
    *result = 1;
    goto out;
  }

  wait_param.timeout = timeout;

  while (1) {
    int err;

    omx__debug_printf("omx_wait going to sleep for %ldms\n", (unsigned long) wait_param.timeout);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);

    omx__debug_printf("omx_wait woken up\n");

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out;
    }

    omx__debug_printf("omx_wait wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out;

    if (omx__test_common(ep, requestp, status)) {
      *result = 1;
      goto out;
    }

    if (wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR
        || wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT) {
      *result = 0;
      goto out;
    }
  }

 out:
  return ret;
}

static inline uint32_t
omx__test_any_common(struct omx_endpoint *ep,
		     uint64_t match_info, uint64_t match_mask,
		     omx_status_t *status)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, match_info);
  union omx_request * req;

  omx__foreach_request(&ep->ctxid[ctxid].done_req_q, req) {
    if (likely((req->generic.status.match_info & match_mask) == match_info)) {
      omx__dequeue_request(&ep->ctxid[ctxid].done_req_q, req);
      memcpy(status, &req->generic.status, sizeof(*status));
      omx__request_free(req);
      return 1;
    }
  }

  return 0;
}

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

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  *result = omx__test_any_common(ep, match_info, match_mask, status);

 out:
  return ret;
}

omx_return_t
omx_wait_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result,
	     uint32_t timeout)
{
  struct omx_cmd_wait_event wait_param;
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

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  if (omx__test_any_common(ep, match_info, match_mask, status)) {
    *result = 1;
    goto out;
  }

  wait_param.timeout = timeout;

  while (1) {
    int err;

    omx__debug_printf("omx_wait_any going to sleep for %ldms\n", (unsigned long) wait_param.timeout);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);

    omx__debug_printf("omx_wait_any woken up\n");

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out;
    }

    omx__debug_printf("omx_wait_any wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out;

    if (omx__test_any_common(ep, match_info, match_mask, status)) {
      *result = 1;
      goto out;
    }

    if (wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR
        || wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT) {
      *result = 0;
      goto out;
    }
  }

 out:
  return ret;
}

static inline uint32_t
omx__ipeek_common(struct omx_endpoint *ep, union omx_request **requestp)
{
  if (unlikely(omx__queue_empty(&ep->ctxid[0].done_req_q))) {
    return 0;
  } else {
    *requestp = omx__queue_first_request(&ep->ctxid[0].done_req_q);
    return 1;
  }
}

omx_return_t
omx_ipeek(struct omx_endpoint *ep, union omx_request **requestp,
	  uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(ep->ctxid_bits)) {
    ret = OMX_NOT_SUPPORTED_WITH_CONTEXT_ID;
    goto out;
  }

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  *result = omx__ipeek_common(ep, requestp);

 out:
  return ret;
}

omx_return_t
omx_peek(struct omx_endpoint *ep, union omx_request **requestp,
	 uint32_t *result, uint32_t timeout)
{
  struct omx_cmd_wait_event wait_param;
  omx_return_t ret = OMX_SUCCESS;

  if (unlikely(ep->ctxid_bits)) {
    ret = OMX_NOT_SUPPORTED_WITH_CONTEXT_ID;
    goto out;
  }

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  if (omx__ipeek_common(ep, requestp)) {
    *result = 1;
    goto out;
  }

  wait_param.timeout = timeout;

  while (1) {
    int err;

    omx__debug_printf("omx_peek going to sleep for %ldms\n", (unsigned long) wait_param.timeout);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);

    omx__debug_printf("omx_peek woken up\n");

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out;
    }

    omx__debug_printf("omx_peek wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out;

    if (omx__ipeek_common(ep, requestp)) {
      *result = 1;
      goto out;
    }

    if (wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR
        || wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT) {
      *result = 0;
      goto out;
    }
  }

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

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  *result = omx__iprobe_common(ep, match_info, match_mask, status);

 out:
  return ret;
}

omx_return_t
omx_probe(struct omx_endpoint *ep,
	  uint64_t match_info, uint64_t match_mask,
	  omx_status_t *status, uint32_t *result,
	  uint32_t timeout)
{
  struct omx_cmd_wait_event wait_param;
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

  ret = omx__progress(ep);
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  if (omx__iprobe_common(ep, match_info, match_mask, status)) {
    *result = 1;
    goto out;
  }

  wait_param.timeout = timeout;

  while (1) {
    int err;

    omx__debug_printf("omx_probe going to sleep for %ldms\n", (unsigned long) wait_param.timeout);

    wait_param.next_exp_event_offset = ep->next_exp_event - ep->exp_eventq;
    wait_param.next_unexp_event_offset = ep->next_unexp_event - ep->unexp_eventq;
    err = ioctl(ep->fd, OMX_CMD_WAIT_EVENT, &wait_param);

    omx__debug_printf("omx_probe woken up\n");

    if (unlikely(err < 0)) {
      ret = omx__errno_to_return("ioctl WAIT_EVENT");
      goto out;
    }

    omx__debug_printf("omx_probe wait event result %d\n", wait_param.status);

    ret = omx__progress(ep);
    if (unlikely(ret != OMX_SUCCESS))
      goto out;

    if (omx__iprobe_common(ep, match_info, match_mask, status)) {
      *result = 1;
      goto out;
    }

    if (wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_INTR
        || wait_param.status == OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT) {
      *result = 0;
      goto out;
    }
  }

 out:
  return ret;
}
