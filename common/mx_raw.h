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
 * This file provides API compatibility wrappers for building
 * native MX applications using the raw interface over Open-MX.
 */

#ifndef MX_RAW_H
#define MX_RAW_H

#define MX_RAW_POLL_SUPPORTED 1

#define MX_RAW_NO_EVENT      0
#define MX_RAW_SEND_COMPLETE 1
#define MX_RAW_RECV_COMPLETE 2

#define MX_DEAD_RECOVERABLE_SRAM_PARITY_ERROR	10
#define MX_DEAD_SRAM_PARITY_ERROR		11
#define MX_DEAD_WATCHDOG_TIMEOUT		12
#define MX_DEAD_COMMAND_TIMEOUT			13
#define MX_DEAD_ENDPOINT_CLOSE_TIMEOUT		14
#define MX_DEAD_ROUTE_UPDATE_TIMEOUT		15
#define MX_DEAD_PCI_PARITY_ERROR		16
#define MX_DEAD_PCI_MASTER_ABORT		17

typedef struct mx_raw_endpoint * mx_raw_endpoint_t;
typedef int mx_raw_status_t;
typedef int mx_endpt_handle_t;

typedef enum {
  MX_HOST_GM = 1,
  MX_HOST_XM = 2,
  MX_HOST_MX = 3,
  MX_HOST_MXvM = 4
} mx_host_type_t;

mx_endpt_handle_t
mx_raw_handle(mx_raw_endpoint_t ep);

mx_return_t
mx_raw_open_endpoint(uint32_t board_number,
		     mx_param_t *params_array, uint32_t params_count,
		     mx_raw_endpoint_t *endpoint);  

mx_return_t
mx_raw_close_endpoint(mx_raw_endpoint_t endpoint);

mx_return_t
mx_raw_send(mx_raw_endpoint_t endpoint, uint32_t physical_port,
	    void *route_pointer, uint32_t route_length,
	    void *send_buffer, uint32_t buffer_length,
	    void *context);

mx_return_t
mx_raw_next_event(mx_raw_endpoint_t endpoint, uint32_t *incoming_port,
		  void **context,
		  void *recv_buffer, uint32_t *recv_bytes,
		  uint32_t timeout_ms,
		  mx_raw_status_t *status);

mx_return_t
mx_raw_set_route_begin(mx_raw_endpoint_t endpoint);

mx_return_t
mx_raw_set_route_end(mx_raw_endpoint_t endpoint);

#define MX_SET_ROUTE_TAKES_MAGID 1

mx_return_t
mx_raw_set_route_mag(mx_raw_endpoint_t endpoint,
		     uint64_t destination_id,
		     void *route, uint32_t route_length,
		     uint32_t input_port, uint32_t output_port,
		     mx_host_type_t host_type,
		     uint32_t mag_id);

mx_return_t
mx_raw_set_route(mx_raw_endpoint_t endpoint,
		 uint64_t destination_id,
		 void *route, uint32_t route_length,
		 uint32_t input_port, uint32_t output_port,
		 mx_host_type_t host_type);

mx_return_t
mx_raw_clear_routes(mx_raw_endpoint_t endpoint,
		    uint64_t destination_id, uint32_t port);

mx_return_t
mx_raw_remove_peer(mx_raw_endpoint_t endpoint,
		   uint64_t destination_id);

mx_return_t
mx_raw_set_map_version(mx_raw_endpoint_t endpoint, uint32_t physical_port,
		       uint64_t mapper_id, uint32_t map_version,
		       uint32_t num_nodes, uint32_t mapping_complete);

mx_return_t
mx_raw_num_ports(mx_raw_endpoint_t endpoint,
		 uint32_t *num_ports);

#define MX_HAS_RAW_LINE_SPEED 1

mx_return_t
mx_raw_line_speed(mx_raw_endpoint_t endpoint, mx_line_speed_t *speed);

mx_return_t
mx_raw_set_hostname(mx_raw_endpoint_t endpoint, char *hostname);

mx_return_t
mx_raw_set_peer_name(mx_raw_endpoint_t endpoint, uint64_t nic_id, char *hostname);

#define MX_HAS_RAW_SET_NIC_REPLY_INFO 1

mx_return_t
mx_raw_set_nic_reply_info(mx_raw_endpoint_t ep, void *blob, uint32_t size); 

#endif /* MX_RAW_H */
