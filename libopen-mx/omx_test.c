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

#include "omx_lib.h"
#include "omx_request.h"

omx_return_t
omx_test(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result)
{
  union omx_request * req = *requestp;
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out;

  if (req->generic.state != OMX_REQUEST_STATE_DONE) {
    *result = 0;
  } else {
    omx__dequeue_request(&ep->done_req_q, req);
    memcpy(status, &req->generic.status, sizeof(*status));

    omx__request_free(req);
    *requestp = NULL;
    *result = 1;
  }

 out:
  return ret;
}

omx_return_t
omx_wait(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result)
{
  union omx_request * req = *requestp;
  omx_return_t ret = OMX_SUCCESS;

  while (req->generic.state != OMX_REQUEST_STATE_DONE) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out;
    /* FIXME: sleep */
  }

  omx__dequeue_request(&ep->done_req_q, req);
  memcpy(status, &req->generic.status, sizeof(*status));

  omx__request_free(req);
  *requestp = NULL;
  *result = 1;

 out:
  return ret;
}

omx_return_t
omx_test_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result)
{
  union omx_request * req;
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out;

  omx__foreach_request(&ep->done_req_q, req) {
    if ((req->generic.status.match_info & match_mask) == match_info) {
      omx__dequeue_request(&ep->done_req_q, req);
      memcpy(status, &req->generic.status, sizeof(*status));
      omx__request_free(req);
      *result = 1;
      goto out;
    }
  }
  *result = 0;

 out:
  return ret;
}

omx_return_t
omx_wait_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result)
{
  union omx_request * req;
  omx_return_t ret = OMX_SUCCESS;

  while (1) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out;
    /* FIXME: sleep */

    omx__foreach_request(&ep->done_req_q, req) {
      if ((req->generic.status.match_info & match_mask) == match_info) {
	omx__dequeue_request(&ep->done_req_q, req);
	memcpy(status, &req->generic.status, sizeof(*status));
	omx__request_free(req);
	*result = 1;
	goto out;
      }
    }
  }
  *result = 0;

 out:
  return ret;
}

omx_return_t
omx_ipeek(struct omx_endpoint *ep, union omx_request **requestp,
	  uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out;

  if (omx__queue_empty(&ep->done_req_q)) {
    *result = 0;
  } else {
    *requestp = omx__queue_first_request(&ep->done_req_q);
    *result = 1;
  }

 out:
  return ret;
}

omx_return_t
omx_peek(struct omx_endpoint *ep, union omx_request **requestp,
	 uint32_t *result)
{
  omx_return_t ret = OMX_SUCCESS;

  while (omx__queue_empty(&ep->done_req_q)) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out;
    /* FIXME: sleep */
  }

  *requestp = omx__queue_first_request(&ep->done_req_q);
  *result = 1;

 out:
  return ret;
}

omx_return_t
omx_iprobe(struct omx_endpoint *ep, uint64_t match_info, uint64_t match_mask,
	   omx_status_t *status, uint32_t *result)
{
  union omx_request * req;
  omx_return_t ret = OMX_SUCCESS;

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out;

  omx__foreach_request(&ep->unexp_req_q, req) {
    if ((req->generic.status.match_info & match_mask) == match_info) {
      memcpy(status, &req->generic.status, sizeof(*status));
      *result = 1;
      goto out;
    }
  }
  *result = 0;

 out:
  return ret;
}

omx_return_t
omx_probe(struct omx_endpoint *ep, uint64_t match_info, uint64_t match_mask,
	  omx_status_t *status, uint32_t *result)
{
  union omx_request * req;
  omx_return_t ret = OMX_SUCCESS;

  while (1) {
    ret = omx__progress(ep);
    if (ret != OMX_SUCCESS)
      goto out;
    /* FIXME: sleep */

    omx__foreach_request(&ep->unexp_req_q, req) {
      if ((req->generic.status.match_info & match_mask) == match_info) {
	memcpy(status, &req->generic.status, sizeof(*status));
	*result = 1;
	goto out;
      }
    }
  }
  *result = 0;

 out:
  return ret;
}
