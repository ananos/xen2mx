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
  OMX_CONNECTION_FAILED, /* FIXME: various error codes for bad endpoint id and closed endpoint */
  OMX_BAD_CONNECTION_KEY,
  OMX_BUSY,
  OMX_BAD_MATCH_MASK,
  OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK,
  OMX_NOT_SUPPORTED_WITH_CONTEXT_ID,
  OMX_NOT_SUPPORTED_IN_HANDLER,
  OMX_CANCEL_NOT_SUPPORTED,
};
typedef enum omx_return omx_return_t;

enum omx_status_code {
  OMX_STATUS_SUCCESS=0,
  OMX_STATUS_TRUNCATED,
  OMX_STATUS_FAILED,
  OMX_STATUS_ENDPOINT_CLOSED,
  OMX_STATUS_ENDPOINT_UNREACHABLE,
  OMX_STATUS_BAD_SESSION,
  OMX_STATUS_BAD_KEY,
  OMX_STATUS_BAD_ENDPOINT,
  OMX_STATUS_BAD_RDMAWIN,
  OMX_STATUS_ABORTED,
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
  uint64_t match_info;
  uint32_t msg_length;
  uint32_t xfer_length;
  void *context;
};
typedef struct omx_status omx_status_t;

#define OMX_API 0x0

omx_return_t
omx__init_api(int api);

static inline omx_return_t omx_init(void) { return omx__init_api(OMX_API); }

omx_return_t
omx_finalize(void);

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

enum omx_endpoint_param_key
{
  OMX_ENDPOINT_PARAM_ERROR_HANDLER = 0,
  OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX = 1,
  OMX_ENDPOINT_PARAM_CONTEXT_ID = 2,
};
typedef enum omx_endpoint_param_key omx_endpoint_param_key_t;

#define OMX_ENDPOINT_CONTEXT_ID_BITS_MAX 16

typedef omx_return_t (*omx_error_handler_t)(char *str, omx_return_t ret);

typedef struct {
  omx_endpoint_param_key_t key;
  union {
    omx_error_handler_t error_handler;
    uint32_t unexp_queue_max;
    struct {
      uint8_t bits;
      uint8_t shift;
    } context_id;
  } val;
} omx_endpoint_param_t;

omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t endpoint_index, uint32_t key,
		  omx_endpoint_param_t * param_array, uint32_t param_count,
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
omx_iconnect(omx_endpoint_t ep,
	     uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	     uint64_t match_info,
	     void *context, omx_request_t *request);

omx_return_t
omx_decompose_endpoint_addr(omx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id);

omx_return_t
omx_set_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void *context);

omx_return_t
omx_get_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void **context);

omx_return_t
omx_isend(omx_endpoint_t ep,
	  void *buffer, size_t length,
	  omx_endpoint_addr_t dest_endpoint,
	  uint64_t match_info,
	  void * context, omx_request_t * request);

omx_return_t
omx_issend(omx_endpoint_t ep,
	   void *buffer, size_t length,
	   omx_endpoint_addr_t dest_endpoint,
	   uint64_t match_info,
	   void * context, omx_request_t * request);

omx_return_t
omx_irecv(omx_endpoint_t ep,
	  void *buffer, size_t length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, omx_request_t * request);

omx_return_t
omx_context(omx_request_t *request, void ** context);

#define OMX_TIMEOUT_INFINITE ((uint32_t) -1)

omx_return_t
omx_test(omx_endpoint_t ep, omx_request_t * request,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_wait(omx_endpoint_t ep, omx_request_t * request,
	 struct omx_status *status, uint32_t * result,
	 uint32_t timeout);

omx_return_t
omx_test_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result);

omx_return_t
omx_wait_any(struct omx_endpoint *ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result,
	     uint32_t timeout);

omx_return_t
omx_ipeek(omx_endpoint_t ep, omx_request_t * request,
	  uint32_t *result);

omx_return_t
omx_peek(omx_endpoint_t ep, omx_request_t * request,
	 uint32_t *result,
	 uint32_t timeout);

omx_return_t
omx_iprobe(struct omx_endpoint *ep, uint64_t match_info, uint64_t match_mask,
	   omx_status_t *status, uint32_t *result);

omx_return_t
omx_probe(struct omx_endpoint *ep, uint64_t match_info, uint64_t match_mask,
	  omx_status_t *status, uint32_t *result,
	  uint32_t timeout);

enum omx_unexp_handler_action {
  OMX_RECV_CONTINUE = 0,
  OMX_RECV_FINISHED
};
typedef enum omx_unexp_handler_action omx_unexp_handler_action_t;

typedef omx_unexp_handler_action_t
(*omx_unexp_handler_t)(void *context, omx_endpoint_addr_t source,
		       uint64_t match_info, uint32_t msg_length,
		       void * data_if_available);

omx_return_t
omx_register_unexp_handler(omx_endpoint_t ep,
			   omx_unexp_handler_t handler,
			   void *context);

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
omx_cancel(omx_endpoint_t ep, omx_request_t *request, uint32_t *result);

omx_return_t
omx_disable_progression(struct omx_endpoint *ep);

omx_return_t
omx_reenable_progression(struct omx_endpoint *ep);

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

omx_return_t
omx_progress(omx_endpoint_t ep);

omx_return_t
omx_set_request_timeout(omx_endpoint_t endpoint,
			omx_request_t request, uint32_t milliseconds);

#endif /* __open_mx_h__ */
