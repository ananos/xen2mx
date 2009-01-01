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

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "open-mx.h"
#include "omx_lib.h"
#include "omx_raw.h"

omx_return_t
omx_raw_open_endpoint(uint32_t board_number,
		      omx_endpoint_param_t *params_array, uint32_t params_count,
		      struct omx_raw_endpoint ** endpoint)
{
  struct omx_cmd_raw_open_endpoint raw_open;
  struct omx_raw_endpoint *ep;
  int fd, err;

  fd = open(OMX_RAW_DEVICE_NAME, O_RDWR);
  if (fd < 0)
    return omx__errno_to_return();

  raw_open.board_index = board_number;
  err = ioctl(fd, OMX_CMD_RAW_OPEN_ENDPOINT, &raw_open);
  if (err < 0) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
							  OMX_BUSY,
							  OMX_INTERNAL_MISC_EINVAL,
							  OMX_INTERNAL_MISC_ENODEV,
							  OMX_SUCCESS,
							  "open board #%d raw endpoint",
							  board_number);
    if (ret == OMX_INTERNAL_MISC_EINVAL)
      ret = OMX_BOARD_NOT_FOUND;
    else if (ret == OMX_INTERNAL_MISC_ENODEV)
      ret = OMX_NO_DRIVER;
    return ret;
  }

  ep = malloc(sizeof(*ep));
  if (!ep)
    return OMX_NO_RESOURCES;

  ep->board_index = board_number;
  ep->fd = fd;

  *endpoint = ep;
  return OMX_SUCCESS;
}

omx_return_t
omx_raw_close_endpoint(struct omx_raw_endpoint * endpoint)
{
  close(endpoint->fd);
  return OMX_SUCCESS;
}

omx_return_t
omx__raw_send(struct omx_raw_endpoint * endpoint,
	      void *send_buffer, uint32_t buffer_length,
	      int need_event, void *event_context)
{
  struct omx_cmd_raw_send raw_send;
  int err;

  raw_send.buffer = (uintptr_t) send_buffer;
  raw_send.buffer_length = buffer_length;
  raw_send.need_event = need_event;
  raw_send.context = (uintptr_t) event_context;

  err = ioctl(endpoint->fd, OMX_CMD_RAW_SEND, &raw_send);
  if (err < 0)
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_BAD_ENDPOINT,
				       OMX_SUCCESS,
				       "send raw message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */

  return OMX_SUCCESS;
}

omx_return_t
omx__raw_next_event(struct omx_raw_endpoint * endpoint, uint32_t *incoming_port,
		    void **context, void *recv_buffer, uint32_t *recv_bytes,
		    uint32_t timeout_ms, omx_raw_status_t *status,
		    int maybe_send)
{
  struct omx_cmd_raw_get_event get_event;
  int err;

  get_event.timeout = timeout_ms;
  get_event.buffer = (uintptr_t) recv_buffer;
  get_event.buffer_length = *recv_bytes;

  err = ioctl(endpoint->fd, OMX_CMD_RAW_GET_EVENT, &get_event);
  if (err < 0)
    return omx__ioctl_errno_to_return_checked(OMX_BAD_ENDPOINT,
					      OMX_SUCCESS,
					      "get raw event");

  if (get_event.status == OMX_CMD_RAW_EVENT_RECV_COMPLETE) {
    *status = OMX_RAW_RECV_COMPLETE;
    *recv_bytes = get_event.buffer_length;
    if (incoming_port)
      *incoming_port = 0;
  } else if (get_event.status == OMX_CMD_RAW_EVENT_SEND_COMPLETE) {
    if (!maybe_send)
      omx__abort(NULL, "Got unexpected raw send complete event");
    *status = OMX_RAW_SEND_COMPLETE;
    if (context)
      *context = (void *)(uintptr_t) get_event.context;
  } else {
    omx__debug_assert(get_event.status == OMX_CMD_RAW_NO_EVENT);
    *status = OMX_RAW_NO_EVENT;
  }

  return OMX_SUCCESS;
}
