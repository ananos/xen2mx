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
#define OMX_NO_FUNC_WRAPPERS
#include "myriexpress.h"
#include "mx_extensions.h"
#include "mx_raw.h"

#include "omx_lib.h"

mx_endpt_handle_t
mx_raw_handle(struct omx_raw_endpoint * ep)
{
  return ep->fd;
}

mx_return_t
mx_raw_open_endpoint(uint32_t board_number,
		     mx_param_t *params_array, uint32_t params_count,
		     struct omx_raw_endpoint **endpoint)
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
  return MX_SUCCESS;
}

mx_return_t
mx_raw_close_endpoint(struct omx_raw_endpoint * endpoint)
{
  close(endpoint->fd);
  return MX_SUCCESS;
}

mx_return_t
mx_raw_send(struct omx_raw_endpoint * endpoint, uint32_t physical_port,
	    void *route_pointer, uint32_t route_length,
	    void *send_buffer, uint32_t buffer_length,
	    void *context)
{
  struct omx_cmd_raw_send raw_send;
  int err;

  raw_send.buffer = (uintptr_t) send_buffer;
  raw_send.buffer_length = buffer_length;
  raw_send.need_event = 1;
  raw_send.context = (uintptr_t) context;

  err = ioctl(endpoint->fd, OMX_CMD_RAW_SEND, &raw_send);
  if (err < 0)
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "send raw message");
    /* if OMX_NO_SYSTEM_RESOURCES, let the retransmission try again later */

  return MX_SUCCESS;
}

mx_return_t
mx_raw_next_event(struct omx_raw_endpoint * endpoint, uint32_t *incoming_port,
		  void **context,
		  void *recv_buffer, uint32_t *recv_bytes,
		  uint32_t timeout_ms,
		  mx_raw_status_t *status)
{
  struct omx_cmd_raw_get_event get_event;
  int err;

  get_event.timeout = timeout_ms;
  get_event.buffer = (uintptr_t) recv_buffer;
  get_event.buffer_length = *recv_bytes;

  err = ioctl(endpoint->fd, OMX_CMD_RAW_GET_EVENT, &get_event);
  if (err < 0)
    return omx__ioctl_errno_to_return_checked(OMX_SUCCESS,
					      "get raw event");

  if (get_event.status == OMX_CMD_RAW_EVENT_RECV_COMPLETE) {
    *status = MX_RAW_RECV_COMPLETE;
    *recv_bytes = get_event.buffer_length;
    if (incoming_port)
      *incoming_port = 0;
  } else if (get_event.status == OMX_CMD_RAW_EVENT_SEND_COMPLETE) {
    *status = MX_RAW_SEND_COMPLETE;
    if (context)
      *context = (void *)(uintptr_t) get_event.context;
  } else {
    omx__debug_assert(get_event.status == OMX_CMD_RAW_NO_EVENT);
    *status = MX_RAW_NO_EVENT;
  }

  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_route_begin(struct omx_raw_endpoint * endpoint)
{
  /* nothing to do */
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_route_end(struct omx_raw_endpoint * endpoint)
{
  /* nothing to do */
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_route_mag(struct omx_raw_endpoint * endpoint,
		     uint64_t destination_id,
		     void *route, uint32_t route_length,
		     uint32_t input_port, uint32_t output_port,
		     mx_host_type_t host_type,
		     uint32_t mag_id)
{
  omx__driver_peer_add(destination_id, NULL);
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_route(struct omx_raw_endpoint * endpoint,
		 uint64_t destination_id,
		 void *route, uint32_t route_length,
		 uint32_t input_port, uint32_t output_port,
		 mx_host_type_t host_type)
{
  omx__driver_peer_add(destination_id, NULL);
  return MX_SUCCESS;
}

mx_return_t
mx_raw_clear_routes(struct omx_raw_endpoint * endpoint,
		    uint64_t destination_id, uint32_t port)
{
  /* nothing to do */
  return MX_SUCCESS;
}

mx_return_t
mx_raw_remove_peer(struct omx_raw_endpoint * endpoint,
		   uint64_t destination_id)
{
  omx__abort("mx_raw_remove_peer not implemented\n");
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_map_version(struct omx_raw_endpoint * endpoint, uint32_t physical_port,
		       uint64_t mapper_id, uint32_t map_version,
		       uint32_t num_nodes, uint32_t mapping_complete)
{
  /* FIXME: nothing to do? */
  return OMX_SUCCESS;
}

mx_return_t
mx_raw_num_ports(struct omx_raw_endpoint * endpoint, uint32_t *num_ports)
{
  *num_ports = 1;
  return OMX_SUCCESS;
}

mx_return_t
mx_raw_line_speed(struct omx_raw_endpoint * endpoint, mx_line_speed_t *speed)
{
  /* FIXME */
  *speed = MX_SPEED_10G;
  return OMX_SUCCESS;
}

mx_return_t
mx_raw_set_hostname(struct omx_raw_endpoint * endpoint, char *hostname)
{
  return omx__driver_set_hostname(endpoint->board_index, hostname);
}

mx_return_t
mx_raw_set_peer_name(struct omx_raw_endpoint * endpoint, uint64_t nic_id, char *hostname)
{
  omx__driver_peer_add(nic_id, hostname);
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_nic_reply_info(struct omx_raw_endpoint * ep, void *blob, uint32_t size)
{
  /* nothing to do */
  return MX_SUCCESS;
}
