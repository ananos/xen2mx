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
 * native MX applications over Open-MX.
 */

#ifndef MYRIEXPRESS_H
#define MYRIEXPRESS_H

#include <inttypes.h>

#include "open-mx.h"

/***********************************
 * Redefine MX constants and macros
 */

#define MX_API 0x301

#define MX_SIZEOF_ADDR OMX_SIZEOF_ADDR

#define MX_ANY_NIC OMX_ANY_NIC
#define MX_ANY_ENDPOINT OMX_ANY_ENDPOINT

#define MX_PARAM_ERROR_HANDLER OMX_ENDPOINT_PARAM_ERROR_HANDLER
#define MX_PARAM_UNEXP_QUEUE_MAX OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX
#define MX_PARAM_CONTEXT_ID OMX_ENDPOINT_PARAM_CONTEXT_ID

#define MX_CONTEXT_ID_BITS_MAX OMX_ENDPOINT_CONTEXT_ID_MAX

#define MX_MATCH_MASK_NONE (~(uint64_t)0)

#define MX_INFINITE OMX_TIMEOUT_INFINITE

#define MX_MAX_HOSTNAME_LEN 80
#define MX_MAX_STR_LEN 128

#define MX_MAX_SEGMENTS OMX_MAX_SEGMENTS

/* macros to help printing uint64_t's */
#define MX_U32(x) \
((sizeof (x) == 8) ? ((uint32_t)((uint64_t)(x) >> 32)) : ((void)(x),0))
#define MX_L32(x) ((uint32_t)(x))

/********************
 * Redefine MX types
 */

typedef omx_endpoint_t mx_endpoint_t;
typedef omx_endpoint_addr_t mx_endpoint_addr_t;
typedef omx_request_t mx_request_t;
typedef omx_endpoint_param_key_t mx_param_key_t;
typedef omx_endpoint_param_t mx_param_t;
typedef omx_error_handler_t mx_error_handler_t;

typedef omx_seg_ptr_t mx_segment_ptr_t;

/* need to be redefined entirely since some fields are renamed,
 * there are some compile-time assertions to check compatibility
 */
struct mx_segment {
  mx_segment_ptr_t segment_ptr;
  uint32_t segment_length;
};
typedef struct mx_segment mx_segment_t;

enum mx_return_code { /* FIXME */
	MX_SUCCESS		= OMX_SUCCESS,
	MX_BAD_BAD_BAD		= OMX_BAD_ERROR,
	MX_FAILURE		= 102, /* FIXME: fix vs OMX_BAD_ERROR */
	MX_ALREADY_INITIALIZED	= OMX_ALREADY_INITIALIZED,
	MX_NOT_INITIALIZED	= OMX_NOT_INITIALIZED,
	MX_NO_DEV		= OMX_NO_DEVICE,
	MX_NO_DRIVER		= 106, /* FIXME: fix vs OMX_NO_DEVICE */
	MX_NO_PERM		= OMX_ACCESS_DENIED,
	MX_BOARD_UNKNOWN	= 108, /* FIXME: use it in omx_open_endpoint and get_info */
	MX_BAD_ENDPOINT		= 109, /* FIXME: use it in omx_close_endpoint */
	MX_BAD_SEG_LIST		= 110, /* unused in MX */
	MX_BAD_SEG_MEM		= 111, /* unused in MX */
	MX_BAD_SEG_CNT		= 112, /* FIXME: use in isend/recv if too many segs */
	MX_BAD_REQUEST		= 113, /* FIXME: use in ibuffered and merge with OMX_CANCEL_NOT_SUPPORTED ? */
	MX_BAD_MATCH_MASK	= OMX_BAD_MATCH_MASK,
	MX_NO_RESOURCES		= OMX_NO_RESOURCES,
	MX_BAD_ADDR_LIST	= 116, /* unused in MX */
	MX_BAD_ADDR_COUNT	= 117, /* unused in MX */
	MX_BAD_ROOT		= 118, /* unused in MX */
	MX_NOT_COMPLETED	= 119, /* unused in MX */
	MX_BUSY			= OMX_BUSY,
	MX_BAD_INFO_KEY		= 121, /* FIXME: use it in get_info */
	MX_BAD_INFO_VAL		= 122, /* FIXME: use it in get_info */
	MX_BAD_NIC		= 123, /* unused in MX */
	MX_BAD_PARAM_LIST	= 124, /* FIXME: use it in omx_open_endpoint */
	MX_BAD_PARAM_NAME	= 125, /* FIXME: use it in omx_open_endpoint */
	MX_BAD_PARAM_VAL	= 126, /* FIXME: use it in omx_open_endpoint */
	MX_BAD_HOSTNAME_ARGS	= 127, /* unused in MX */
	MX_HOST_NOT_FOUND	= 128, /* FIXME: use in hostname_from/to_nic_id */
	MX_REQUEST_PENDING	= 129, /* unused in MX */
	MX_TIMEOUT		= OMX_TIMEOUT,
	MX_NO_MATCH		= 131, /* unused in MX */
	MX_BAD_ENDPOINT_ID	= OMX_REMOTE_ENDPOINT_BAD_ID,
	MX_CONNECTION_FAILED	= OMX_REMOTE_ENDPOINT_CLOSED,
	MX_BAD_CONNECTION_KEY	= OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY,
	MX_BAD_INFO_LENGTH	= 135, /* FIXME: use it in get_info */
	MX_NIC_NOT_FOUND	= 136, /* FIXME: use it in connect_common */
	MX_BAD_KERNEL_VERSION	= 137, /* FIXME: use it in init */
	MX_BAD_LIB_VERSION	= 138, /* FIXME: use it in init */
	MX_NIC_DEAD		= 139, /* useless for Open-MX ? */
	MX_CANCEL_NOT_SUPPORTED	= OMX_CANCEL_NOT_SUPPORTED,
	MX_CLOSE_IN_HANDLER	= OMX_NOT_SUPPORTED_IN_HANDLER,
	MX_BAD_MATCHING_FOR_CONTEXT_ID_MASK	= OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK,
	MX_NOT_SUPPORTED_WITH_CONTEXT_ID	= OMX_NOT_SUPPORTED_WITH_CONTEXT_ID
};
typedef enum mx_return_code mx_return_t;

enum mx_status_code { /* FIXME */
	MX_STATUS_SUCCESS	= OMX_SUCCESS,
	MX_STATUS_PENDING	= 101, /* unused in MX */
	MX_STATUS_BUFFERED	= 102, /* unused in MX */
	MX_STATUS_REJECTED	= 103, /* unused in MX */
	MX_STATUS_TIMEOUT	= OMX_TIMEOUT,
	MX_STATUS_TRUNCATED	= OMX_MESSAGE_TRUNCATED,
	MX_STATUS_CANCELLED	= 106, /* unused in MX */
	MX_STATUS_ENDPOINT_UNKNOWN	= 107, /* unused in MX */
	MX_STATUS_ENDPOINT_CLOSED	= OMX_REMOTE_ENDPOINT_CLOSED,
	MX_STATUS_ENDPOINT_UNREACHABLE	= OMX_REMOTE_ENDPOINT_UNREACHABLE,
	MX_STATUS_BAD_SESSION	= OMX_REMOTE_ENDPOINT_BAD_SESSION,
	MX_STATUS_BAD_KEY	= OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY,
	MX_STATUS_BAD_ENDPOINT	= OMX_REMOTE_ENDPOINT_BAD_ID,
	MX_STATUS_BAD_RDMAWIN	= OMX_REMOTE_RDMA_WINDOW_BAD_ID,
	MX_STATUS_ABORTED	= OMX_MESSAGE_ABORTED,
	MX_STATUS_EVENTQ_FULL	= 115, /* unused in MX */
	MX_STATUS_NO_RESOURCES	= OMX_NO_RESOURCES
};
typedef enum mx_status_code mx_status_code_t;

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

typedef void (*mx_matching_callback_t)(void *context, uint64_t match_value, int length);

/**********************************************************
 * MX API prototypes (needed for symbol-referenced compat)
 */

#define mx_init() mx__init_api(MX_API)
extern mx_return_t mx__init_api(int);
extern void mx_finalize(void);
extern mx_error_handler_t mx_set_error_handler(mx_error_handler_t);

extern mx_return_t mx_open_endpoint(uint32_t board_number, uint32_t endpoint_id,
				    uint32_t endpoint_key, mx_param_t *params_array, uint32_t params_count,
				    mx_endpoint_t *endpoint);
extern mx_return_t mx_close_endpoint(mx_endpoint_t endpoint);
extern mx_return_t mx_wakeup(mx_endpoint_t endpoint);
extern mx_return_t mx_disable_progression(mx_endpoint_t ep);
extern mx_return_t mx_reenable_progression(mx_endpoint_t ep);

extern mx_return_t mx_isend(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
			    mx_endpoint_addr_t dest_endpoint, uint64_t match_info, void *context,
			    mx_request_t *request);
extern mx_return_t mx_issend(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
			     mx_endpoint_addr_t dest_endpoint, uint64_t match_info, void *context,
			     mx_request_t *request);
extern mx_return_t mx_irecv(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
			    uint64_t match_info, uint64_t match_mask, void *context,
			    mx_request_t *request);

extern mx_return_t mx_cancel(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result);
extern mx_return_t mx_test(mx_endpoint_t ep, mx_request_t * request, mx_status_t * status, uint32_t * result);
extern mx_return_t mx_wait(mx_endpoint_t endpoint, mx_request_t *request, uint32_t timeout, mx_status_t *status, uint32_t *result);
extern mx_return_t mx_test_any(mx_endpoint_t endpoint, uint64_t match_info, uint64_t match_mask, mx_status_t *status, uint32_t *result);
extern mx_return_t mx_wait_any(mx_endpoint_t endpoint, uint32_t timeout, uint64_t match_info, uint64_t match_mask, mx_status_t *status, uint32_t *result);
extern mx_return_t mx_ipeek(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result);
extern mx_return_t mx_peek(mx_endpoint_t endpoint, uint32_t timeout, mx_request_t *request, uint32_t *result);
extern mx_return_t mx_iprobe(mx_endpoint_t endpoint, uint64_t match_info, uint64_t match_mask, mx_status_t *status, uint32_t *result);
extern mx_return_t mx_probe(mx_endpoint_t endpoint, uint32_t timeout, uint64_t match_info, uint64_t match_mask, mx_status_t *status, uint32_t *result);
extern mx_return_t mx_ibuffered(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result);

extern mx_return_t mx_context(mx_request_t *request, void **context);
extern mx_return_t mx_get_info(mx_endpoint_t ep, mx_get_info_key_t key, void *in_val, uint32_t in_len, void *out_val, uint32_t out_len);

extern mx_return_t mx_hostname_to_nic_id(char *hostname, uint64_t *nic_id);
extern mx_return_t mx_board_number_to_nic_id(uint32_t board_number, uint64_t *nic_id);
extern mx_return_t mx_nic_id_to_board_number(uint64_t nic_id, uint32_t *board_number);
extern mx_return_t mx_nic_id_to_hostname(uint64_t nic_id, char *hostname);

extern mx_return_t mx_connect(mx_endpoint_t endpoint, uint64_t nic_id, uint32_t endpoint_id,
			      uint32_t key, uint32_t timeout, mx_endpoint_addr_t *addr);
extern mx_return_t mx_decompose_endpoint_addr(mx_endpoint_addr_t endpoint_addr, uint64_t *nic_id, uint32_t *endpoint_id);
extern mx_return_t mx_get_endpoint_addr(mx_endpoint_t endpoint, mx_endpoint_addr_t *endpoint_addr);

extern const char * mx_strerror(mx_return_t return_code);
extern const char * mx_strstatus(mx_status_code_t status);

#ifdef OMX_MX_API_UNSUPPORTED_COMPAT
/*
 * Not implemented yet
 */
extern mx_return_t mx_register_unexp_callback(mx_endpoint_t ep, mx_matching_callback_t cb, void *ctxt);
extern mx_return_t mx_iput(mx_endpoint_t endpoint, void *local_addr, uint32_t length,
			   mx_endpoint_addr_t dest_endpoint, uint64_t remote_addr, void *context,
			   mx_request_t *request);
extern mx_return_t mx_iget(mx_endpoint_t endpoint, void *local_addr, uint32_t length,
			   mx_endpoint_addr_t dest_endpoint, uint64_t remote_addr, void *context,
			   mx_request_t *request);
extern mx_return_t mx_buffered(mx_endpoint_t endpoint, mx_request_t *request, uint32_t timeout, uint32_t *result);
#endif

/******************************************
 * MX API wrappers (needed for API compat)
 */

#ifndef OMX_NO_FUNC_WRAPPERS
/*
 * only include the following replacements when NOT
 * building the ABI compat stuff in the lib (and
 * NOT using it in an external code)
 */

#define mx__init_api(api) omx__init_api(api)
#define mx_init() mx__init_api(MX_API)
#define mx_finalize() omx_finalize()

#define mx_set_error_handler(hdlr) omx_set_error_handler(NULL, hdlr)
#define MX_ERRORS_ARE_FATAL OMX_ERRORS_ARE_FATAL
#define MX_ERRORS_RETURN OMX_ERRORS_RETURN

#define mx_open_endpoint(bn,eid,key,pa,pc,ep) omx_open_endpoint(bn,eid,key,pa,pc,ep)
#define mx_close_endpoint(ep) omx_close_endpoint(ep)

#define mx_wakeup(ep) omx_wakeup(ep)

#define mx_disable_progression(ep) omx_disable_progression(ep)
#define mx_reenable_progression(ep) omx_reenable_progression(ep)

#define mx_isend(ep, segs, nseg, d, mi, c, r) omx_isendv(ep, (omx_seg_t *) (void *) segs, nseg, d, mi, c, r)
#define mx_issend(ep, segs, nseg, d, mi, c, r) omx_issendv(ep, (omx_seg_t *) (void *) segs, nseg, d, mi, c, r)
#define mx_irecv(ep, segs, nseg, mi, mm, c, r) omx_irecvv(ep, (omx_seg_t *) (void *) segs, nseg, mi, mm, c, r)

#define mx_cancel(ep,req,res) omx_cancel(ep,req,res)

#define mx_test(endpoint, request, status, result) \
  omx_test(endpoint, request, (omx_status_t *) (void *) status, result)
#define mx_wait(endpoint, request, timeout, status, result) \
  omx_wait(endpoint, request, (omx_status_t *) (void *) status, result, timeout)

#define mx_test_any(endpoint, match_info, match_mask, status, result) \
  omx_test_any(endpoint, match_info, match_mask, (omx_status_t *) (void *) status, result)
#define mx_wait_any(endpoint, timeout, match_info, match_mask, status, result) \
  omx_wait_any(endpoint, match_info, match_mask, (omx_status_t *) (void *) status, result, timeout)

#define mx_ipeek(endpoint, request, result) \
  omx_ipeek(endpoint, request, result)
#define mx_peek(endpoint, timeout, request, result) \
  omx_peek(endpoint, request, result, timeout)

#define mx_iprobe(endpoint, match_info, match_mask, status, result) \
  omx_iprobe(endpoint, match_info, match_mask, (omx_status_t *) (void *) status, result)
#define mx_probe(endpoint, timeout, match_info, match_mask, status, result) \
  omx_probe(endpoint, match_info, match_mask, (omx_status_t *) (void *) status, result, timeout)

#define mx_ibuffered(endpoint, request, result) omx_ibuffered(endpoint, request, result)
/* FIXME: mx_buffered */

#define mx_context(req,ctx) omx_context(req,ctx)

#define mx_hostname_to_nic_id(h,ni) omx_hostname_to_nic_id(h,ni)
#define mx_board_number_to_nic_id(bn,ni) omx_board_number_to_nic_id(bn,ni)
#define mx_nic_id_to_board_number(ni,bn) omx_nic_id_to_board_number(ni,bn)
#define mx_nic_id_to_hostname(ni,h) omx_nic_id_to_hostname(ni,h)

#define mx_connect(ep,nicid,eid,key,timeout,addr) omx_connect(ep,nicid,eid,key,timeout,addr)
#define mx_decompose_endpoint_addr(addr,nicid,eid) omx_decompose_endpoint_addr(addr,nicid,eid)
#define mx_get_endpoint_addr(ep,addr) omx_get_endpoint_addr(ep,addr)

#define mx_strerror(ret) omx_strerror(ret)
#define mx_strstatus(code) omx_strerror(code)

#endif /* !OMX_NO_FUNC_WRAPPERS */

#endif /* MYRIEXPRESS_H */
