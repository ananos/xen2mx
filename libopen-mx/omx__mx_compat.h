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

extern mx_return_t
omx_unlikely_return_to_mx(omx_return_t omxret);

static inline mx_return_t
omx_return_to_mx(omx_return_t omxret)
{
  if (likely(omxret == OMX_SUCCESS))
    return MX_SUCCESS;
  else
    return omx_unlikely_return_to_mx(omxret);
}

extern omx_return_t
omx_unlikely_return_from_mx(mx_return_t mxret);

static inline omx_return_t
omx_return_from_mx(mx_return_t mxret)
{
  if (likely(mxret == MX_SUCCESS))
    return OMX_SUCCESS;
  else
    return omx_unlikely_return_from_mx(mxret);
}

extern mx_status_code_t
omx_unlikely_status_code_to_mx(omx_return_t omxret);

static inline mx_status_code_t
omx_status_code_to_mx(omx_return_t omxret)
{
  if (likely(omxret == OMX_SUCCESS))
    return MX_STATUS_SUCCESS;
  else
    return omx_unlikely_status_code_to_mx(omxret);
}

extern omx_return_t
omx_unlikely_status_code_from_mx(mx_status_code_t mxcode);

static inline omx_return_t
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

static inline uint32_t
omx_timeout_from_mx(uint32_t mx_timeout)
{
  if (mx_timeout == MX_INFINITE)
    return OMX_TIMEOUT_INFINITE;
  else
    return mx_timeout;
}

static inline void
omx_status_to_mx(struct mx_status *mxst, struct omx_status *omxst)
{
  memcpy(mxst, omxst, sizeof(*mxst));
  mxst->code = omx_status_code_to_mx(omxst->code);
}

#define omx_raw_endpoint_ptr_from_mx(epp) ((omx_raw_endpoint_t *) (void *) (epp))
#define omx_raw_endpoint_from_mx(ep) ((omx_raw_endpoint_t) (ep))

#define omx_raw_status_ptr_from_mx(code) ((omx_raw_status_t *) (void *) (code))

#endif /* __omx__mx_compat_h__ */
