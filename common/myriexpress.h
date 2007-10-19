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

/*
 * This file provides API compatibility wrappers for building
 * native MX applications over Open-MX.
 */

#ifndef MYRIEXPRESS_H
#define MYRIEXPRESS_H

#include <assert.h>
#include "open-mx.h"

#define MX_API OMX_API

typedef omx_endpoint_t mx_endpoint_t;

#define MX_SIZEOF_ADDR OMX_SIZEOF_ADDR

typedef omx_endpoint_addr_t mx_endpoint_addr_t;

#define MX_ANY_NIC OMX_ANY_NIC
#define MX_ANY_ENDPOINT OMX_ANY_ENDPOINT

typedef omx_request_t mx_request_t;

enum mx_return_code { /* FIXME */
	MX_SUCCESS		= OMX_SUCCESS,
	MX_BAD_BAD_BAD		= OMX_BAD_ERROR,
	MX_FAILURE		= 102,
	MX_ALREADY_INITIALIZED	= OMX_ALREADY_INITIALIZED,
	MX_NOT_INITIALIZED	= OMX_NOT_INITIALIZED,
	MX_NO_DEV		= OMX_NO_DEVICE,
	MX_NO_DRIVER		= 106,
	MX_NO_PERM		= OMX_ACCESS_DENIED,
	MX_BOARD_UNKNOWN	= 108,
	MX_BAD_ENDPOINT		= 109,
	MX_BAD_SEG_LIST		= 110,
	MX_BAD_SEG_MEM		= 111,
	MX_BAD_SEG_CNT		= 112,
	MX_BAD_REQUEST		= 113,
	MX_BAD_MATCH_MASK	= 114,
	MX_NO_RESOURCES		= OMX_NO_RESOURCES,
	MX_BAD_ADDR_LIST	= 116,
	MX_BAD_ADDR_COUNT	= 117,
	MX_BAD_ROOT		= 118,
	MX_NOT_COMPLETED	= 119,
	MX_BUSY			= OMX_BUSY,
	MX_BAD_INFO_KEY		= 121,
	MX_BAD_INFO_VAL		= 122,
	MX_BAD_NIC		= 123,
	MX_BAD_PARAM_LIST	= 124,
	MX_BAD_PARAM_NAME	= 125,
	MX_BAD_PARAM_VAL	= 126,
	MX_BAD_HOSTNAME_ARGS	= 127,
	MX_HOST_NOT_FOUND	= 128,
	MX_REQUEST_PENDING	= 129,
	MX_TIMEOUT		= 130,
	MX_NO_MATCH		= 131,
	MX_BAD_ENDPOINT_ID	= 132,
	MX_CONNECTION_FAILED	= 133,
	MX_BAD_CONNECTION_KEY	= OMX_BAD_CONNECTION_KEY,
	MX_BAD_INFO_LENGTH	= 135,
	MX_NIC_NOT_FOUND	= 136,
	MX_BAD_KERNEL_VERSION	= 137,
	MX_BAD_LIB_VERSION	= 138,
	MX_NIC_DEAD		= 139,
	MX_CANCEL_NOT_SUPPORTED	= OMX_CANCEL_NOT_SUPPORTED,
	MX_CLOSE_IN_HANDLER	= OMX_NOT_SUPPORTED_IN_HANDLER,
	MX_BAD_MATCHING_FOR_CONTEXT_ID_MASK	= 142,
	MX_NOT_SUPPORTED_WITH_CONTEXT_ID	= 143
};
typedef enum mx_return_code mx_return_t;

#define MX_PARAM_ERROR_HANDLER OMX_ENDPOINT_PARAM_ERROR_HANDLER
#define MX_PARAM_UNEXP_QUEUE_MAX OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX
#define MX_PARAM_CONTEXT_ID OMX_ENDPOINT_PARAM_CONTEXT_ID
typedef omx_endpoint_param_key_t mx_param_key_t;
typedef omx_endpoint_param_t mx_param_t;

#define MX_CONTEXT_ID_BITS_MAX OMX_ENDPOINT_CONTEXT_ID_MAX

typedef omx_error_handler_t mx_error_handler_t;

enum mx_status_code { /* FIXME */
	MX_STATUS_SUCCESS	= OMX_STATUS_SUCCESS,
	MX_STATUS_PENDING	= 101,
	MX_STATUS_BUFFERED	= 102,
	MX_STATUS_REJECTED	= 103,
	MX_STATUS_TIMEOUT	= 104,
	MX_STATUS_TRUNCATED	= OMX_STATUS_TRUNCATED,
	MX_STATUS_CANCELLED	= 106,
	MX_STATUS_ENDPOINT_UNKNOWN	= 107,
	MX_STATUS_ENDPOINT_CLOSED	= OMX_STATUS_ENDPOINT_CLOSED,
	MX_STATUS_ENDPOINT_UNREACHABLE	= 109,
	MX_STATUS_BAD_SESSION	= OMX_STATUS_BAD_SESSION,
	MX_STATUS_BAD_KEY	= OMX_STATUS_BAD_KEY,
	MX_STATUS_BAD_ENDPOINT	= OMX_STATUS_BAD_ENDPOINT,
	MX_STATUS_BAD_RDMAWIN	= OMX_STATUS_BAD_RDMAWIN,
	MX_STATUS_ABORTED	= OMX_STATUS_ABORTED,
	MX_STATUS_EVENTQ_FULL	= 115,
	MX_STATUS_NO_RESOURCES	= 116
};
typedef enum mx_status_code mx_status_code_t;

#if 1
/* need to be redefined entirely since some fields are renamed,
 * there are some compile-time assertions to check compatibility
 */
struct mx_status {
  mx_status_code_t code;
  mx_endpoint_addr_t source;
  uint64_t match_info;
  uint32_t msg_length;
  uint32_t xfer_length;
  void *context;
};
typedef struct mx_status mx_status_t;
#else
typedef omx_status_t mx_status_t;
#endif

#define mx__init_api omx__init_api
#define mx_init() mx__init_api(MX_API)
#define mx_finalize omx_finalize

#define MX_ERRORS_RETURN 0 /* FIXME */
#define mx_set_error_handler(...) MX_SUCCESS; /* FIXME */

#define mx_open_endpoint omx_open_endpoint
#define mx_close_endpoint omx_close_endpoint

#define mx_wakeup(...) MX_SUCCESS; /* FIXME */

#define mx_disable_progression omx_disable_progression
#define mx_reenable_progression omx_reenable_progression

/* FIXME: wrapper instead, once supported */
typedef void * mx_segment_ptr_t;
struct mx_segment {
  mx_segment_ptr_t segment_ptr;
  uint32_t segment_length;
};
typedef struct mx_segment mx_segment_t;

#define MX_MAX_SEGMENTS 1 /* FIXME */

static inline mx_return_t
mx_isend(mx_endpoint_t endpoint,
	 mx_segment_t *segments_list,
	 uint32_t segments_count,
	 mx_endpoint_addr_t dest_endpoint,
	 uint64_t match_info,
	 void *context,
	 mx_request_t *request)
{
  void * buffer = NULL;
  uint32_t length = 0;
  assert(segments_count <= 1);
  if (segments_count) {
    if (!segments_list) return OMX_INVALID_PARAMETER;
    buffer = segments_list[0].segment_ptr;
    length = segments_list[0].segment_length;
  }
  return omx_isend(endpoint, buffer, length, dest_endpoint, match_info, context, request);
}

static inline mx_return_t
mx_issend(mx_endpoint_t endpoint,
	 mx_segment_t *segments_list,
	 uint32_t segments_count,
	 mx_endpoint_addr_t dest_endpoint,
	 uint64_t match_info,
	 void *context,
	 mx_request_t *request)
{
  void * buffer = NULL;
  uint32_t length = 0;
  assert(segments_count <= 1);
  if (segments_count) {
    if (!segments_list) return OMX_INVALID_PARAMETER;
    buffer = segments_list[0].segment_ptr;
    length = segments_list[0].segment_length;
  }
  return omx_issend(endpoint, buffer, length, dest_endpoint, match_info, context, request);
}

#define MX_MATCH_MASK_NONE (~(uint64_t)0)

static inline mx_return_t
mx_irecv(mx_endpoint_t endpoint,
	 mx_segment_t *segments_list,
	 uint32_t segments_count,
	 uint64_t match_info,
	 uint64_t match_mask,
	 void *context,
	 mx_request_t *request)
{
  void * buffer = NULL;
  uint32_t length = 0;
  assert(segments_count <= 1);
  if (segments_count) {
    if (!segments_list) return OMX_INVALID_PARAMETER;
    buffer = segments_list[0].segment_ptr;
    length = segments_list[0].segment_length;
  }
  return omx_irecv(endpoint, buffer, length, match_info, match_mask, context, request);
}

#define mx_cancel omx_cancel

#define MX_INFINITE OMX_TIMEOUT_INFINITE

#define mx_test(endpoint, request, status, result) \
  omx_test(endpoint, request, (struct omx_status *) status, result)
#define mx_wait(endpoint, request, timeout, status, result) \
  omx_wait(endpoint, request, (struct omx_status *) status, result, timeout)

#define mx_test_any(endpoint, match_info, match_mask, status, result) \
  omx_test_any(endpoint, match_info, match_mask, (struct omx_status *) status, result)
#define mx_wait_any(endpoint, timeout, match_info, match_mask, status, result) \
  omx_wait_any(endpoint, match_info, match_mask, (struct omx_status *) status, result, timeout)

#define mx_ipeek omx_ipeek
#define mx_peek(endpoint, timeout, request, result) \
  omx_peek(endpoint, request, result, timeout)

#define mx_iprobe(endpoint, match_info, match_mask, status, result) \
  omx_iprobe(endpoint, match_info, match_mask, (struct omx_status *) status, result)
#define mx_probe(endpoint, timeout, match_info, match_mask, status, result) \
  omx_probe(endpoint, match_info, match_mask, (struct omx_status *) status, result, timeout)

/* FIXME: mx_ibuffered */
/* FIXME: mx_buffered */

#define mx_context omx_context

enum mx_net_type {
  MX_NET_MYRI,
  MX_NET_ETHER,
};
typedef enum mx_net_type mx_net_type_t;

enum mx_line_speed {
  MX_SPEED_2G,
  MX_SPEED_10G,
  MX_SPEED_OPEN_MX,
};
typedef enum mx_line_speed mx_line_speed_t;

enum mx_get_info_key {
  MX_NIC_COUNT = 1,
  MX_NIC_IDS = 2,
  MX_MAX_NATIVE_ENDPOINTS = 3,
  MX_NATIVE_REQUESTS = 4,
  MX_COUNTERS_COUNT = 5,
  MX_COUNTERS_LABELS = 6,
  MX_COUNTERS_VALUES = 7,
  MX_PRODUCT_CODE = 8,
  MX_PART_NUMBER = 9,
  MX_SERIAL_NUMBER = 10,
  MX_PORT_COUNT = 11,
  MX_PIO_SEND_MAX = 12,
  MX_COPY_SEND_MAX = 13,
  MX_NUMA_NODE = 14,
#define MX_HAS_NET_TYPE
  MX_NET_TYPE = 15,
  MX_LINE_SPEED = 16
};
typedef enum mx_get_info_key mx_get_info_key_t;

static inline mx_return_t
mx_get_info(mx_endpoint_t ep, mx_get_info_key_t key,
	    void *in_val, uint32_t in_len,
	    void *out_val, uint32_t out_len)
{
  switch (key) {
  case MX_NIC_COUNT:
    return omx_get_info(ep, OMX_INFO_BOARD_COUNT, in_val, in_len, out_val, out_len);

  case MX_NIC_IDS:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_MAX_NATIVE_ENDPOINTS:
    return omx_get_info(ep, OMX_INFO_ENDPOINT_MAX, in_val, in_len, out_val, out_len);

  case MX_NATIVE_REQUESTS:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_COUNTERS_COUNT:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_COUNTERS_LABELS:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_COUNTERS_VALUES:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_PRODUCT_CODE:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_PART_NUMBER:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_SERIAL_NUMBER:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_PORT_COUNT:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_PIO_SEND_MAX:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_COPY_SEND_MAX:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_NUMA_NODE:
    return MX_BAD_BAD_BAD; /* TODO */

  case MX_NET_TYPE:
    * (uint32_t *) out_val = MX_NET_ETHER;
    return MX_SUCCESS;

  case MX_LINE_SPEED:
    * (uint32_t *) out_val = MX_SPEED_OPEN_MX;
    return MX_SUCCESS;

  }

  return MX_BAD_INFO_KEY;
}

#define MX_MAX_HOSTNAME_LEN 80

#define mx_hostname_to_nic_id omx_hostname_to_nic_id
#define mx_board_number_to_nic_id omx_board_number_to_nic_id
#define mx_nic_id_to_board_number omx_nic_id_to_board_number
#define mx_nic_id_to_hostname omx_nic_id_to_hostname

#define mx_connect omx_connect
#define mx_decompose_endpoint_addr omx_decompose_endpoint_addr
#define mx_get_endpoint_addr omx_get_endpoint_addr

#define mx_strerror omx_strerror
#define mx_strstatus omx_strstatus

#endif /* MYRIEXPRESS_H */
