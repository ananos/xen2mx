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

/* FIXME: return codes */

enum mx_return_code
{
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
	MX_CANCEL_NOT_SUPPORTED	= 140,
	MX_CLOSE_IN_HANDLER	= 141,
	MX_BAD_MATCHING_FOR_CONTEXT_ID_MASK	= 142,
	MX_NOT_SUPPORTED_WITH_CONTEXT_ID	= 143
};

#define MX_PARAM_ERROR_HANDLER OMX_ENDPOINT_PARAM_ERROR_HANDLER
#define MX_PARAM_UNEXP_QUEUE_MAX OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX
#define MX_PARAM_CONTEXT_ID OMX_ENDPOINT_PARAM_CONTEXT_ID
typedef omx_endpoint_param_key_t mx_param_key_t;
typedef omx_endpoint_param_t mx_param_t;

#define MX_CONTEXT_ID_BITS_MAX OMX_ENDPOINT_CONTEXT_ID_MAX

typedef omx_error_handler_t mx_error_handler_t;
/* FIXME: error handler values and mx_set_error_handler */

/* FIXME: status codes */

enum mx_status_code
{
	MX_STATUS_SUCCESS	= OMX_SUCCESS,
	MX_STATUS_PENDING	= 101,
	MX_STATUS_BUFFERED	= 102,
	MX_STATUS_REJECTED	= 103,
	MX_STATUS_TIMEOUT	= 104,
	MX_STATUS_TRUNCATED	= 105,
	MX_STATUS_CANCELLED	= 106,
	MX_STATUS_ENDPOINT_UNKNOWN	= 107,
	MX_STATUS_ENDPOINT_CLOSED	= 108,
	MX_STATUS_ENDPOINT_UNREACHABLE	= 109,
	MX_STATUS_BAD_SESSION	= 110,
	MX_STATUS_BAD_KEY	= OMX_STATUS_BAD_KEY,
	MX_STATUS_BAD_ENDPOINT	= 112,
	MX_STATUS_BAD_RDMAWIN	= 113,
	MX_STATUS_ABORTED	= 114,
	MX_STATUS_EVENTQ_FULL	= 115,
	MX_STATUS_NO_RESOURCES	= 116
};

typedef enum mx_status_code mx_status_code_t;

typedef omx_status_t mx_status_t;

#define mx__init_api omx__init_api
#define mx_init() mx__init_api(MX_API)
#define mx_finalize omx_finalize

#define MX_ERRORS_RETURN 0
#define mx_set_error_handler(...) MX_SUCCESS;

#define mx_open_endpoint omx_open_endpoint
#define mx_close_endpoint omx_close_endpoint

/* FIXME: mx_wakeup */
#define mx_wakeup(...) MX_SUCCESS;

/* FIXME: mx_disable_progression */
/* FIXME: mx_reenable_progression */

/* FIXME: wrapper instead */
typedef void * mx_segment_ptr_t;
typedef struct
{
  mx_segment_ptr_t segment_ptr;
  uint32_t segment_length;
}
mx_segment_t;

/* define infinite timeout for mx_wait */
  
#define MX_INFINITE   0

/* FIXME: MX_MAX_SEGMENTS */
#define MX_MAX_SEGMENTS 1


static inline mx_return_t
mx_isend(mx_endpoint_t endpoint,
	 mx_segment_t *segments_list,
	 uint32_t segments_count,
	 mx_endpoint_addr_t dest_endpoint,
	 uint64_t match_info,
	 void *context,
	 mx_request_t *request)
{
  assert(segments_count == 1);
  return omx_isend(endpoint, segments_list[0].segment_ptr, segments_list[0].segment_length, match_info, dest_endpoint, context, request);
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
  assert(segments_count == 1);
  return omx_issend(endpoint, segments_list[0].segment_ptr, segments_list[0].segment_length, match_info, dest_endpoint, context, request);
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
  assert(segments_count == 1);
  return omx_irecv(endpoint, segments_list[0].segment_ptr, segments_list[0].segment_length, match_info, match_mask, context, request);
}

/* FIXME: mx_cancel */
#define mx_cancel(...) MX_FAILURE

#define mx_test omx_test
#define mx_wait(endpoint,request,timeout, status,result) omx_wait(endpoint,request,status,result)
#define mx_test_any omx_test_any
#define mx_wait_any(endpoint,timeout,match_info,match_mask,status,result) omx_wait_any(endpoint,match_info,match_mask,status,result)
#define mx_ipeek omx_ipeek
#define mx_peek(endpoint,timeout,request,result) omx_peek(endpoint,request,result)
#define mx_iprobe omx_iprobe
#define mx_probe(endpoint,timeout,match_info,match_mask,status,result) omx_probe(endpoint,match_info,match_mask,status,result)

/* FIXME: mx_ibuffered */
/* FIXME: mx_buffered */

/* FIXME: mx_context */

/* FIXME: mx_line_speed_t mx_net_type */
/* FIXME: mx_get_info_key_t */
/* FIXME: mx_get_info */

/* FIXME: MX_MAX_HOSTNAME_LEN */
#define MX_MAX_HOSTNAME_LEN 80

#define mx_hostname_to_nic_id omx_hostname_to_nic_id
#define mx_board_number_to_nic_id omx_board_number_to_nic_id
#define mx_nic_id_to_board_number omx_nic_id_to_board_number
#define mx_nic_id_to_hostname omx_nic_id_to_hostname

#define mx_iconnect omx_iconnect
#define mx_connect omx_connect
#define mx_decompose_endpoint_addr omx_decompose_endpoint_addr
#define mx_get_endpoint_addr omx_get_endpoint_addr

#define mx_strerror omx_strerror
#define mx_strstatus omx_strstatus

/* FIXME: mx_disconnect */

#define MX_RECV_CONTINUE OMX_RECV_CONTINUE
#define MX_RECV_FINISHED OMX_RECV_FINISHED
typedef omx_unexp_handler_action_t mx_unexp_handler_action_t;
typedef omx_unexp_handler_t mx_unexp_handler_t;
#define mx_register_unexp_handler omx_register_unexp_handler

/* FIXME: mx_forget */
/* FIXME: mx_progress */

#define mx_set_endpoint_addr_context omx_set_endpoint_addr_context
#define mx_get_endpoint_addr_context omx_get_endpoint_addr_context

/* FIXME: mx_set_request_timeout */

#endif /* MYRIEXPRESS_H */
