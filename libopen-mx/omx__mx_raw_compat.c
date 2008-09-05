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

/* load MX specific types */
#include "omx__mx_compat.h"

/* load Open-MX specific types */
#include "open-mx.h"
#include "omx_lib.h"
#include "omx_raw.h"

mx_endpt_handle_t
mx_raw_handle(mx_raw_endpoint_t ep)
{
  return (mx_endpt_handle_t) ((omx_raw_endpoint_from_mx(ep))->fd);
}

mx_return_t
mx_raw_open_endpoint(uint32_t board_number,
		     mx_param_t *params_array, uint32_t params_count,
		     mx_raw_endpoint_t *endpoint)
{
  omx_return_t omxret;
  omxret = omx_raw_open_endpoint(board_number,
				 omx_endpoint_param_ptr_from_mx(params_array), params_count,
				 omx_raw_endpoint_ptr_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_raw_close_endpoint(mx_raw_endpoint_t endpoint)
{
  omx_return_t omxret;
  omxret = omx_raw_close_endpoint(omx_raw_endpoint_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_raw_send(mx_raw_endpoint_t endpoint, uint32_t physical_port,
	    void *route_pointer, uint32_t route_length,
	    void *send_buffer, uint32_t buffer_length,
	    void *context)
{
  omx_return_t omxret;
  omxret = omx__raw_send(omx_raw_endpoint_from_mx(endpoint),
			 send_buffer, buffer_length,
			 1, context);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_raw_next_event(mx_raw_endpoint_t endpoint, uint32_t *incoming_port,
		  void **context,
		  void *recv_buffer, uint32_t *recv_bytes,
		  uint32_t timeout_ms,
		  mx_raw_status_t *status)
{
  omx_return_t omxret;
  omxret = omx__raw_next_event(omx_raw_endpoint_from_mx(endpoint),
			       incoming_port, context,
			       recv_buffer, recv_bytes,
			       timeout_ms,
			       omx_raw_status_ptr_from_mx(status),
			       1);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_raw_set_route_begin(mx_raw_endpoint_t endpoint)
{
  /* nothing to do */
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_route_end(mx_raw_endpoint_t endpoint)
{
  /* nothing to do */
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_route_mag(mx_raw_endpoint_t endpoint,
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
mx_raw_set_route(mx_raw_endpoint_t endpoint,
		 uint64_t destination_id,
		 void *route, uint32_t route_length,
		 uint32_t input_port, uint32_t output_port,
		 mx_host_type_t host_type)
{
  omx__driver_peer_add(destination_id, NULL);
  return MX_SUCCESS;
}

mx_return_t
mx_raw_clear_routes(mx_raw_endpoint_t endpoint,
		    uint64_t destination_id, uint32_t port)
{
  /* nothing to do */
  return MX_SUCCESS;
}

mx_return_t
mx_raw_remove_peer(mx_raw_endpoint_t endpoint,
		   uint64_t destination_id)
{
  omx__abort(NULL, "mx_raw_remove_peer not implemented\n");
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_map_version(mx_raw_endpoint_t endpoint, uint32_t physical_port,
		       uint64_t mapper_id, uint32_t map_version,
		       uint32_t num_nodes, uint32_t mapping_complete)
{
  omx_return_t omxret;
  omxret = omx__driver_set_peer_table_state(mapping_complete, map_version, num_nodes, mapper_id);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_raw_num_ports(mx_raw_endpoint_t endpoint, uint32_t *num_ports)
{
  *num_ports = 1;
  return MX_SUCCESS;
}

mx_return_t
mx_raw_line_speed(mx_raw_endpoint_t endpoint, mx_line_speed_t *speed)
{
  /* FIXME */
  *speed = MX_SPEED_10G;
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_hostname(mx_raw_endpoint_t endpoint, char *hostname)
{
  omx_return_t omxret;
  omxret = omx__driver_set_hostname(omx_raw_endpoint_from_mx(endpoint)->board_index,
				    hostname);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_raw_set_peer_name(mx_raw_endpoint_t endpoint, uint64_t nic_id, char *hostname)
{
  omx__driver_peer_add(nic_id, hostname);
  return MX_SUCCESS;
}

mx_return_t
mx_raw_set_nic_reply_info(mx_raw_endpoint_t ep, void *blob, uint32_t size)
{
  /* nothing to do */
  return MX_SUCCESS;
}
