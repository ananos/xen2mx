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

/*
 * This file copies the MX typedefs from myriexpress.h and mx_raw.h
 * in Myricom's MX distribution.
 */

#ifndef __omx__mx_compat_h__
#define __omx__mx_compat_h__

#include <stdint.h>

#include "mx/myriexpress.h"
#include "mx/mx_extensions.h"
#include "mx/mx_raw.h"

/***********************
 * API conversion tools
 */

#include "open-mx.h"
#include "omx_lib.h"

extern __pure mx_return_t
omx_unlikely_return_to_mx(omx_return_t omxret, int strict);

static inline __pure mx_return_t
omx_return_to_mx(omx_return_t omxret)
{
  /* check the size of enums */
  BUILD_BUG_ON(sizeof(mx_return_t) != sizeof(omx_return_t));

  if (likely(omxret == OMX_SUCCESS))
    return MX_SUCCESS;
  else
    return omx_unlikely_return_to_mx(omxret, 1);
}

/* convert to hacky MX return that may also be a MX status, used by the error handler */
static inline __pure mx_return_t
omx_error_to_mx(omx_return_t omxret)
{
  if (likely(omxret == OMX_SUCCESS))
    return MX_SUCCESS;
  else
    return omx_unlikely_return_to_mx(omxret, 0);
}

extern __pure omx_return_t
omx_unlikely_return_from_mx(mx_return_t mxret, int strict);

static inline __pure omx_return_t
omx_return_from_mx(mx_return_t mxret)
{
  if (likely(mxret == MX_SUCCESS))
    return OMX_SUCCESS;
  else
    return omx_unlikely_return_from_mx(mxret, 1);
}

/* convert from hacky MX return that may also be a MX status, used by the error handler */
static inline __pure omx_return_t
omx_error_from_mx(mx_return_t mxret)
{
  if (likely(mxret == MX_SUCCESS))
    return OMX_SUCCESS;
  else
    return omx_unlikely_return_from_mx(mxret, 0);
}

extern __pure mx_status_code_t
omx_unlikely_status_code_to_mx(omx_return_t omxret);

static inline __pure mx_status_code_t
omx_status_code_to_mx(omx_return_t omxret)
{
  /* check the size of enums */
  BUILD_BUG_ON(sizeof(mx_status_code_t) != sizeof(omx_return_t));

  if (likely(omxret == OMX_SUCCESS))
    return MX_STATUS_SUCCESS;
  else
    return omx_unlikely_status_code_to_mx(omxret);
}

extern __pure omx_return_t
omx_unlikely_status_code_from_mx(mx_status_code_t mxcode);

static inline __pure omx_return_t
omx_status_code_from_mx(mx_status_code_t mxcode)
{
  if (likely(mxcode == MX_STATUS_SUCCESS))
    return OMX_SUCCESS;
  else
    return omx_unlikely_status_code_from_mx(mxcode);
}

#define omx_endpoint_ptr_from_mx(epp) ((omx_endpoint_t *) (void *) (epp))
#define omx_endpoint_from_mx(ep) ((omx_endpoint_t) (ep))

#define omx_endpoint_param_ptr_from_mx(paramp) ((omx_endpoint_param_t *) (void *) (paramp))

#define omx_error_handler_to_mx(hdlr) ((mx_error_handler_t) (hdlr))
#define omx_error_handler_from_mx(hdlr) ((omx_error_handler_t) (hdlr))

#define omx_unexp_handler_from_mx(hdlr) ((omx_unexp_handler_t) (hdlr))

#define omx_seg_ptr_from_mx(segp) ((struct omx_seg *) (void *) (segp))

#define omx_endpoint_addr_from_mx(addr) (* (omx_endpoint_addr_t *) (void *) &(addr))
#define omx_endpoint_addr_ptr_from_mx(addr) ((omx_endpoint_addr_t *) (void *) (addr))

#define omx_request_ptr_from_mx(reqp) ((omx_request_t *) (void *) (reqp))
#define omx_request_from_mx(req) ((omx_request_t) (req))

static inline __pure uint32_t
omx_timeout_from_mx(uint32_t mx_timeout)
{
  if (mx_timeout == MX_INFINITE)
    return OMX_TIMEOUT_INFINITE;
  else
    return mx_timeout;
}

static inline void
omx_status_to_mx(struct mx_status *mxst, const struct omx_status *omxst)
{
  /* check the contents of status types, since their fields are different */
  BUILD_BUG_ON(sizeof(mx_status_t) != sizeof(omx_status_t));
  BUILD_BUG_ON(offsetof(mx_status_t, code) != offsetof(omx_status_t, code));
  BUILD_BUG_ON(sizeof(((mx_status_t*)NULL)->code) != sizeof(((omx_status_t*)NULL)->code));
  BUILD_BUG_ON(offsetof(mx_status_t, source) != offsetof(omx_status_t, addr));
  BUILD_BUG_ON(sizeof(((mx_status_t*)NULL)->source) != sizeof(((omx_status_t*)NULL)->addr));
  BUILD_BUG_ON(offsetof(mx_status_t, match_info) != offsetof(omx_status_t, match_info));
  BUILD_BUG_ON(sizeof(((mx_status_t*)NULL)->match_info) != sizeof(((omx_status_t*)NULL)->match_info));
  BUILD_BUG_ON(offsetof(mx_status_t, msg_length) != offsetof(omx_status_t, msg_length));
  BUILD_BUG_ON(sizeof(((mx_status_t*)NULL)->msg_length) != sizeof(((omx_status_t*)NULL)->msg_length));
  BUILD_BUG_ON(offsetof(mx_status_t, xfer_length) != offsetof(omx_status_t, xfer_length));
  BUILD_BUG_ON(sizeof(((mx_status_t*)NULL)->xfer_length) != sizeof(((omx_status_t*)NULL)->xfer_length));
  BUILD_BUG_ON(offsetof(mx_status_t, context) != offsetof(omx_status_t, context));
  BUILD_BUG_ON(sizeof(((mx_status_t*)NULL)->context) != sizeof(((omx_status_t*)NULL)->context));

  memcpy(mxst, omxst, sizeof(*mxst));
  mxst->code = omx_status_code_to_mx(omxst->code);
}

#define omx_raw_endpoint_ptr_from_mx(epp) ((omx_raw_endpoint_t *) (void *) (epp))
#define omx_raw_endpoint_from_mx(ep) ((omx_raw_endpoint_t) (ep))

#define omx_raw_status_ptr_from_mx(code) ((omx_raw_status_t *) (void *) (code))

#endif /* __omx__mx_compat_h__ */
