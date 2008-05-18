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
  return omx_raw_open_endpoint(board_number, params_array, params_count, endpoint);
}

mx_return_t
mx_raw_close_endpoint(struct omx_raw_endpoint * endpoint)
{
  return omx_raw_close_endpoint(endpoint);
}

mx_return_t
mx_raw_send(struct omx_raw_endpoint * endpoint, uint32_t physical_port,
	    void *route_pointer, uint32_t route_length,
	    void *send_buffer, uint32_t buffer_length,
	    void *context)
{
  return omx__raw_send(endpoint, send_buffer, buffer_length, 1, context);
}

mx_return_t
mx_raw_next_event(struct omx_raw_endpoint * endpoint, uint32_t *incoming_port,
		  void **context,
		  void *recv_buffer, uint32_t *recv_bytes,
		  uint32_t timeout_ms,
		  mx_raw_status_t *status)
{
  return omx__raw_next_event(endpoint, incoming_port, context, recv_buffer, recv_bytes, timeout_ms, status, 1);
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
  return omx__driver_set_peer_table_state(mapping_complete, map_version, num_nodes, mapper_id);
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
