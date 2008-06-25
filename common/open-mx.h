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

#ifndef __open_mx_h__
#define __open_mx_h__

#include <stdint.h>
#include <stdlib.h>

typedef struct omx_endpoint * omx_endpoint_t;

typedef union omx_request * omx_request_t;

enum omx_return {
  OMX_SUCCESS = 0,
  OMX_BAD_ERROR = 1, /* only used for unexpected errno from open/mmap */

  OMX_ALREADY_INITIALIZED = 3,
  OMX_NOT_INITIALIZED = 4,
  OMX_NO_DEVICE_FILE = 5,
  OMX_NO_DRIVER = 6,
  OMX_ACCESS_DENIED = 7,
  OMX_BOARD_NOT_FOUND = 8,
  OMX_BAD_ENDPOINT = 9,

  OMX_SEGMENTS_BAD_COUNT = 12,

  OMX_BAD_REQUEST = 13,
  OMX_BAD_MATCH_MASK = 14,
  OMX_NO_RESOURCES = 15,

  OMX_BUSY = 20,
  OMX_BAD_INFO_KEY = 21,
  OMX_BAD_INFO_ADDRESS = 22,

  OMX_ENDPOINT_PARAMS_BAD_LIST = 24,
  OMX_ENDPOINT_PARAM_BAD_KEY = 25,
  OMX_ENDPOINT_PARAM_BAD_VALUE = 26,

  OMX_PEER_NOT_FOUND = 28,

  OMX_TIMEOUT = 30,

  OMX_REMOTE_ENDPOINT_BAD_ID = 32,
  OMX_REMOTE_ENDPOINT_CLOSED = 33,
  OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY = 34,
  OMX_BAD_INFO_LENGTH = 35,
  OMX_NIC_ID_NOT_FOUND = 36,
  OMX_BAD_KERNEL_ABI = 37,
  OMX_BAD_LIB_ABI = 38,

  OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK = 42,

  OMX_REMOTE_RDMA_WINDOW_BAD_ID = 91,
  OMX_REMOTE_ENDPOINT_UNREACHABLE = 92,
  OMX_REMOTE_ENDPOINT_BAD_SESSION = 93,
  OMX_MESSAGE_ABORTED = 94,
  OMX_MESSAGE_TRUNCATED = 95,
  OMX_NOT_SUPPORTED_IN_HANDLER = 96,
  OMX_NO_SYSTEM_RESOURCES = 97,

  OMX_NOT_IMPLEMENTED = 99,
  OMX_RETURN_CODE_MAX = 100,
};
typedef enum omx_return omx_return_t;

#define OMX_SIZEOF_ADDR 16

struct omx_endpoint_addr {
  uint64_t data[OMX_SIZEOF_ADDR/sizeof(uint64_t)];
};
typedef struct omx_endpoint_addr omx_endpoint_addr_t;

struct omx_status {
  enum omx_return code;
  omx_endpoint_addr_t addr;
  uint64_t match_info;
  uint32_t msg_length;
  uint32_t xfer_length;
  void *context;
};
typedef struct omx_status omx_status_t;

#define OMX_API 0x301

omx_return_t
omx__init_api(int api);

static inline omx_return_t omx_init(void) { return omx__init_api(OMX_API); }

omx_return_t
omx_finalize(void);

const char *
omx_strerror(omx_return_t ret);

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

omx_error_handler_t
omx_set_error_handler(omx_endpoint_t ep, omx_error_handler_t handler);

extern const omx_error_handler_t OMX_ERRORS_ARE_FATAL;
extern const omx_error_handler_t OMX_ERRORS_RETURN;

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
omx_wakeup(omx_endpoint_t ep);

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
omx_disconnect(omx_endpoint_t ep, omx_endpoint_addr_t addr);

omx_return_t
omx_decompose_endpoint_addr(omx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id);

omx_return_t
omx_decompose_endpoint_addr_with_session(omx_endpoint_addr_t endpoint_addr,
					 uint64_t *nic_id, uint32_t *endpoint_id, uint32_t *session_id);

omx_return_t
omx_set_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void *context);

omx_return_t
omx_get_endpoint_addr_context(omx_endpoint_addr_t endpoint_addr,
			      void **context);

#define OMX_MAX_SEGMENTS 256

typedef void * omx_seg_ptr_t;

struct omx_seg {
  omx_seg_ptr_t ptr;
  uint32_t len;
};
typedef struct omx_seg omx_seg_t;

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
omx_isendv(omx_endpoint_t ep,
	   omx_seg_t *segs, uint32_t nseg,
	   omx_endpoint_addr_t dest_endpoint,
	   uint64_t match_info,
	   void * context, omx_request_t * request);

omx_return_t
omx_issendv(omx_endpoint_t ep,
	    omx_seg_t *segs, uint32_t nseg,
	    omx_endpoint_addr_t dest_endpoint,
	    uint64_t match_info,
	    void * context, omx_request_t * request);

omx_return_t
omx_irecvv(omx_endpoint_t ep,
	   omx_seg_t *segs, uint32_t nseg,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, omx_request_t * request);

omx_return_t
omx_context(omx_request_t *request, void ** context);

#define OMX_TIMEOUT_INFINITE ((uint32_t) -1)

omx_return_t
omx_test(omx_endpoint_t ep, omx_request_t * request,
	 omx_status_t *status, uint32_t * result);

omx_return_t
omx_wait(omx_endpoint_t ep, omx_request_t * request,
	 omx_status_t *status, uint32_t * result,
	 uint32_t timeout);

omx_return_t
omx_test_any(omx_endpoint_t ep,
	     uint64_t match_info, uint64_t match_mask,
	     omx_status_t *status, uint32_t *result);

omx_return_t
omx_wait_any(omx_endpoint_t ep,
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
omx_iprobe(omx_endpoint_t ep, uint64_t match_info, uint64_t match_mask,
	   omx_status_t *status, uint32_t *result);

omx_return_t
omx_probe(omx_endpoint_t ep, uint64_t match_info, uint64_t match_mask,
	  omx_status_t *status, uint32_t *result,
	  uint32_t timeout);

omx_return_t
omx_ibuffered(omx_endpoint_t ep, omx_request_t *request, uint32_t * result);

enum omx_unexp_handler_action {
  OMX_UNEXP_HANDLER_RECV_CONTINUE = 0,
  OMX_UNEXP_HANDLER_RECV_FINISHED
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
  /* returns an array of board address */
  OMX_INFO_BOARD_IDS,
  /* return the board hostname of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_HOSTNAME,
  /* return the board iface name of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_IFACENAME,
  /* return the numa node of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_NUMA_NODE,
  /* returns the number of counters */
  OMX_INFO_COUNTER_MAX,
  /* returns the values of all counters */
  OMX_INFO_COUNTER_VALUES,
  /* returns the label of a counter */
  OMX_INFO_COUNTER_LABEL,
};
typedef enum omx_info_key omx_info_key_t;

omx_return_t
omx_cancel(omx_endpoint_t ep, omx_request_t *request, uint32_t *result);

omx_return_t
omx_forget(omx_endpoint_t ep, omx_request_t *request);

omx_return_t
omx_disable_progression(omx_endpoint_t ep);

omx_return_t
omx_reenable_progression(omx_endpoint_t ep);

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
