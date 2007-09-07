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

#include "open-mx.h"

#define MX_API OMX_API

typedef omx_endpoint_t mx_endpoint_t;

#define MX_SIZEOF_ADDR OMX_SIZEOF_ADDR

typedef omx_endpoint_addr_t mx_endpoint_addr_t;

#define MX_ANY_NIC OMX_ANY_NIC
#define MX_ANY_ENDPOINT OMX_ANY_ENDPOINT

typedef omx_request_t mx_request_t;

/* FIXME: return codes */

typedef omx_return_t mx_return_t;

#define MX_PARAM_ERROR_HANDLER OMX_ENDPOINT_PARAM_ERROR_HANDLER
#define MX_PARAM_UNEXP_QUEUE_MAX OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX
#define MX_PARAM_CONTEXT_ID OMX_ENDPOINT_PARAM_CONTEXT_ID
typedef omx_endpoint_param_key_t mx_param_key_t;
typedef omx_endpoint_param_t mx_param_t;

#define MX_CONTEXT_ID_BITS_MAX OMX_ENDPOINT_CONTEXT_ID_MAX

typedef omx_error_handler_t mx_error_handler_t;
/* FIXME: error handler values and mx_set_error_handler */

/* FIXME: status codes */

typedef omx_status_t mx_status_t;

#define mx__init_apit omx__init_api
#define mx_finalize omx_finalize

#define mx_open_endpoint omx_open_endpoint
#define mx_close_endpoint omx_close_endpoint

/* FIXME: mx_wakeup */

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

/* FIXME: MX_MAX_SEGMENTS */

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
  return omx_isend(endpoint, segments_list[0].segments_ptr, segment_list[0].segment_length, dest_endpoint, match_info, context, request);
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
  return omx_issend(endpoint, segments_list[0].segment_ptr, segments_list[0].segment_length, dest_endpoint, match_info, context, request);
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

#define mx_hostname_to_nic_id omx_hostname_to_nic_id
#define mx_board_number_to_nic_id omx_board_number_to_nic_id
#define mx_nic_id_to_board_number omx_nic_id_to_board_number
#define mx_nic_id_to_hostname omx_nic_id_to_hostname

#define mx_iconnect omx_iconnect
#define mx_connect omx_connect
#define mx_decompose_endpoint_addr omx_decompose_endpoint_addr
#define mx_get_endpoint_add omx_get_endpoint_addr

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
