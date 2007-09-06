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

#ifndef __open_mx_h__
#define __open_mx_h__

#include <stdint.h>

typedef struct omx_endpoint * omx_endpoint_t;

typedef union omx_request * omx_request_t;

enum omx_return {
  OMX_SUCCESS=0,
  OMX_BAD_ERROR,
  OMX_ALREADY_INITIALIZED,
  OMX_NOT_INITIALIZED,
  OMX_NO_DEVICE,
  OMX_ACCESS_DENIED,
  OMX_NO_RESOURCES,
  OMX_NO_SYSTEM_RESOURCES,
  OMX_INVALID_PARAMETER,
  OMX_NOT_IMPLEMENTED,
  OMX_BAD_CONNECTION_KEY,
  OMX_BUSY,
};
typedef enum omx_return omx_return_t;

enum omx_status_code {
  OMX_STATUS_SUCCESS=0,
  OMX_STATUS_FAILED,
  OMX_STATUS_BAD_KEY,
};
typedef enum omx_status_code omx_status_code_t;

#define OMX_SIZEOF_ADDR 16

struct omx_endpoint_addr {
  char data[OMX_SIZEOF_ADDR];
};
typedef struct omx_endpoint_addr omx_endpoint_addr_t;

struct omx_status {
  enum omx_status_code code;
  omx_endpoint_addr_t addr;
  unsigned long msg_length;
  unsigned long xfer_length;
  uint64_t match_info;
  void *context;
};
typedef struct omx_status omx_status_t;

#define OMX_API 0x0

omx_return_t
omx__init_api(int api);

static inline omx_return_t omx_init(void) { return omx__init_api(OMX_API); }

const char *
omx_strerror(omx_return_t ret);

const char *
omx_strstatus(omx_status_code_t code);

omx_return_t
omx_board_number_to_nic_id(uint32_t board_number,
			   uint64_t *nic_id);

omx_return_t
omx_nic_id_to_board_number(uint64_t nic_id,
			   uint32_t *board_number);

#define OMX_ANY_NIC 0xffffffffU
#define OMX_ANY_ENDPOINT 0xffffffffU

omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t index, uint32_t key,
		  omx_endpoint_t *epp);

omx_return_t
omx_close_endpoint(omx_endpoint_t ep);

omx_return_t
omx_get_endpoint_addr(omx_endpoint_t endpoint,
		      omx_endpoint_addr_t *endpoint_addr);

omx_return_t
omx_connect(omx_endpoint_t endpoint,
	    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	    uint32_t timeout,
	    omx_endpoint_addr_t *addr);

omx_return_t
omx_decompose_endpoint_addr(omx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id);

omx_return_t
omx_isend(omx_endpoint_t ep,
	  void *buffer, size_t length,
	  uint64_t match_info,
	  omx_endpoint_addr_t dest_endpoint,
	  void * context, omx_request_t * request);

omx_return_t
omx_irecv(omx_endpoint_t ep,
	  void *buffer, size_t length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, omx_request_t * request);

omx_return_t
omx_test(omx_endpoint_t ep, omx_request_t * request,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_wait(omx_endpoint_t ep, omx_request_t * request,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_test_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result);

omx_return_t
omx_wait_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result);

omx_return_t
omx_ipeek(omx_endpoint_t ep, omx_request_t * request,
	  uint32_t *result);

omx_return_t
omx_peek(omx_endpoint_t ep, omx_request_t * request,
	 uint32_t *result);

enum omx_info_key {
  /* return the maximum number of boards */
  OMX_INFO_BOARD_MAX,
  /* return the maximum number of endpoints per board */
  OMX_INFO_ENDPOINT_MAX,
  /* return the current number of boards */
  OMX_INFO_BOARD_COUNT,
  /* return the board hostname of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_HOSTNAME,
  /* return the board iface name of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_IFACENAME,
};
typedef enum omx_info_key omx_info_key_t;

omx_return_t
omx_get_info(omx_endpoint_t ep, omx_info_key_t key,
	     const void * in_val, uint32_t in_len,
	     void * out_val, uint32_t out_len);

#define OMX_HOSTNAMELEN_MAX 80

#define OMX_BOARD_ADDR_STRLEN 18

omx_return_t
omx_hostname_to_nic_id(char *hostname,
		       uint64_t *board_addr);

omx_return_t
omx_nic_id_to_hostname(uint64_t board_addr,
		       char *hostname);

#endif /* __open_mx_h__ */
