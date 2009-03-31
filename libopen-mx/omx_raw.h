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
 * This file is shipped within the Open-MX library when the MX API compat is
 * enabled in order to expose MX RAW API symbols names since they may be required
 * to build the FMS.
 */

#ifndef __omx_raw_h__
#define __omx_raw_h__

#include "open-mx.h"

struct omx_raw_endpoint {
  int board_index;
  int fd;
};
typedef struct omx_raw_endpoint * omx_raw_endpoint_t;

#define OMX_RAW_NO_EVENT      0
#define OMX_RAW_SEND_COMPLETE 1
#define OMX_RAW_RECV_COMPLETE 2

typedef int omx_raw_status_t;

omx_return_t
omx_raw_open_endpoint(uint32_t board_number,
		      const omx_endpoint_param_t *params_array, uint32_t params_count,
		      omx_raw_endpoint_t * endpoint);

omx_return_t
omx_raw_close_endpoint(omx_raw_endpoint_t endpoint);

omx_return_t
omx__raw_send(omx_raw_endpoint_t endpoint,
	      const void *send_buffer, uint32_t buffer_length,
	      int need_event, const void *event_context);

static inline omx_return_t
omx_raw_send(omx_raw_endpoint_t endpoint,
	     const void *send_buffer, uint32_t buffer_length)
{
  return omx__raw_send(endpoint, send_buffer, buffer_length, 0, NULL);
}

omx_return_t
omx__raw_next_event(struct omx_raw_endpoint * endpoint, uint32_t *incoming_port,
		    void **context, void *recv_buffer, uint32_t *recv_bytes,
		    uint32_t timeout_ms, omx_raw_status_t *status,
		    int maybe_send);

static inline omx_return_t
omx_raw_next_event(struct omx_raw_endpoint * endpoint,
		   void *recv_buffer, uint32_t *recv_bytes,
		   uint32_t timeout_ms, omx_raw_status_t *status)
{
  return omx__raw_next_event(endpoint, NULL, NULL, recv_buffer, recv_bytes, timeout_ms, status, 0);
}

#endif /* __omx_raw_h__ */
