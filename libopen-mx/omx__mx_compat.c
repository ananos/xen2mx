/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

/* load MX specific types */
#include "omx__mx_compat.h"

/* load Open-MX specific types */
#include "open-mx.h"
#include "omx_lib.h"

/**************************************
 * Non-inlined ABI conversion routines
 */

/*
 * These functions convert non-success return values and status codes.
 * The whole functions supporting success are inlined in the header.
 */

__pure mx_return_t
omx_unlikely_return_to_mx(omx_return_t omxret)
{
  switch (omxret) {
  case OMX_BAD_ERROR:
    return MX_BAD_BAD_BAD;
  case OMX_ALREADY_INITIALIZED:
    return MX_ALREADY_INITIALIZED;
  case OMX_NOT_INITIALIZED:
    return MX_NOT_INITIALIZED;
  case OMX_NO_DEVICE_FILE:
    return MX_NO_DEV;
  case OMX_NO_DRIVER:
    return MX_NO_DRIVER;
  case OMX_ACCESS_DENIED:
    return MX_NO_PERM;
  case OMX_BOARD_NOT_FOUND:
    return MX_BOARD_UNKNOWN;
  case OMX_BAD_ENDPOINT:
    return MX_BAD_ENDPOINT;
  case OMX_SEGMENTS_BAD_COUNT:
    return MX_BAD_SEG_CNT;
  case OMX_BAD_REQUEST:
    return MX_BAD_REQUEST; /* can be MX_CANCEL_NOT_SUPPORTED as well */
  case OMX_BAD_MATCH_MASK:
    return MX_BAD_MATCH_MASK;
  case OMX_NO_RESOURCES:
    return MX_NO_RESOURCES;
  case OMX_BUSY:
    return MX_BUSY;
  case OMX_BAD_INFO_KEY:
    return MX_BAD_INFO_KEY;
  case OMX_BAD_INFO_ADDRESS:
    return MX_BAD_INFO_VAL;
  case OMX_ENDPOINT_PARAMS_BAD_LIST:
    return MX_BAD_PARAM_LIST;
  case OMX_ENDPOINT_PARAM_BAD_KEY:
    return MX_BAD_PARAM_NAME;
  case OMX_ENDPOINT_PARAM_BAD_VALUE:
    return MX_BAD_PARAM_VAL;
  case OMX_PEER_NOT_FOUND:
    return MX_HOST_NOT_FOUND;
  case OMX_TIMEOUT:
    return MX_TIMEOUT;
  case OMX_REMOTE_ENDPOINT_BAD_ID:
    return MX_BAD_ENDPOINT_ID;
  case OMX_REMOTE_ENDPOINT_CLOSED:
    return MX_CONNECTION_FAILED;
  case OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY:
    return MX_BAD_CONNECTION_KEY;
  case OMX_BAD_INFO_LENGTH:
    return MX_BAD_INFO_LENGTH;
  case OMX_NIC_ID_NOT_FOUND:
    return MX_NIC_NOT_FOUND;
  case OMX_BAD_KERNEL_ABI:
    return MX_BAD_KERNEL_VERSION;
  case OMX_BAD_LIB_ABI:
    return MX_BAD_LIB_VERSION;
  case OMX_NOT_SUPPORTED_IN_HANDLER:
    return MX_CLOSE_IN_HANDLER;
  case OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK:
    return MX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
  default:
    omx__abort(NULL, "Unexpected Open-MX return code %d to translate into MX\n", omxret);
  }
}

__pure omx_return_t
omx_unlikely_return_from_mx(mx_return_t mxret)
{
  switch (mxret) {
  case MX_BAD_BAD_BAD:
    return OMX_BAD_ERROR;
  case MX_ALREADY_INITIALIZED:
    return OMX_ALREADY_INITIALIZED;
  case MX_NOT_INITIALIZED:
    return OMX_NOT_INITIALIZED;
  case MX_NO_DEV:
    return OMX_NO_DEVICE_FILE;
  case MX_NO_DRIVER:
    return OMX_NO_DRIVER;
  case MX_NO_PERM:
    return OMX_ACCESS_DENIED;
  case MX_BOARD_UNKNOWN:
    return OMX_BOARD_NOT_FOUND;
  case MX_BAD_ENDPOINT:
    return OMX_BAD_ENDPOINT;
  case MX_BAD_SEG_CNT:
    return OMX_SEGMENTS_BAD_COUNT;
  case MX_BAD_REQUEST:
    return OMX_BAD_REQUEST;
  case MX_BAD_MATCH_MASK:
    return OMX_BAD_MATCH_MASK;
  case MX_NO_RESOURCES:
    return OMX_NO_RESOURCES;
  case MX_BUSY:
    return OMX_BUSY;
  case MX_BAD_INFO_KEY:
    return OMX_BAD_INFO_KEY;
  case MX_BAD_INFO_VAL:
    return OMX_BAD_INFO_ADDRESS;
  case MX_BAD_PARAM_LIST:
    return OMX_ENDPOINT_PARAMS_BAD_LIST;
  case MX_BAD_PARAM_NAME:
    return OMX_ENDPOINT_PARAM_BAD_KEY;
  case MX_BAD_PARAM_VAL:
    return OMX_ENDPOINT_PARAM_BAD_VALUE;
  case MX_HOST_NOT_FOUND:
    return OMX_PEER_NOT_FOUND;
  case MX_TIMEOUT:
    return OMX_TIMEOUT;
  case MX_BAD_ENDPOINT_ID:
    return OMX_REMOTE_ENDPOINT_BAD_ID;
  case MX_CONNECTION_FAILED:
    return OMX_REMOTE_ENDPOINT_CLOSED;
  case MX_BAD_CONNECTION_KEY:
    return OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY;
  case MX_BAD_INFO_LENGTH:
    return OMX_BAD_INFO_LENGTH;
  case MX_NIC_NOT_FOUND:
    return OMX_NIC_ID_NOT_FOUND;
  case MX_BAD_KERNEL_VERSION:
    return OMX_BAD_KERNEL_ABI;
  case MX_BAD_LIB_VERSION:
    return OMX_BAD_LIB_ABI;
  case MX_CANCEL_NOT_SUPPORTED:
    return OMX_BAD_REQUEST;
  case MX_CLOSE_IN_HANDLER:
    return OMX_NOT_SUPPORTED_IN_HANDLER;
  case MX_BAD_MATCHING_FOR_CONTEXT_ID_MASK:
    return OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK;
  default:
    omx__abort(NULL, "Unexpected MX return code %d to translate into Open-MX\n", mxret);
  }
}

__pure mx_status_code_t
omx_unlikely_status_code_to_mx(omx_return_t omxret)
{
  switch (omxret) {
  case OMX_TIMEOUT:
    return MX_STATUS_TIMEOUT;
  case OMX_MESSAGE_TRUNCATED:
    return MX_STATUS_TRUNCATED;
  case OMX_CANCELLED:
    return MX_STATUS_CANCELLED;
  case OMX_REMOTE_ENDPOINT_CLOSED:
    return MX_STATUS_ENDPOINT_CLOSED;
  case OMX_REMOTE_ENDPOINT_UNREACHABLE:
    return MX_STATUS_ENDPOINT_UNREACHABLE;
  case OMX_REMOTE_ENDPOINT_BAD_SESSION:
    return MX_STATUS_BAD_SESSION;
  case OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY:
    return MX_STATUS_BAD_KEY;
  case OMX_REMOTE_ENDPOINT_BAD_ID:
    return MX_STATUS_BAD_ENDPOINT;
  case OMX_REMOTE_RDMA_WINDOW_BAD_ID:
    return MX_STATUS_BAD_RDMAWIN;
  case OMX_MESSAGE_ABORTED:
    return MX_STATUS_ABORTED;
  case OMX_NO_RESOURCES:
    return MX_STATUS_NO_RESOURCES;
  default:
    omx__abort(NULL, "Unexpected Open-MX return code %d to translate into MX status code\n", omxret);
  }
}

__pure omx_return_t
omx_unlikely_status_code_from_mx(mx_status_code_t mxcode)
{
  switch (mxcode) {
  case MX_STATUS_TIMEOUT:
    return OMX_TIMEOUT;
  case MX_STATUS_TRUNCATED:
    return OMX_MESSAGE_TRUNCATED;
  case MX_STATUS_CANCELLED:
    return OMX_CANCELLED;
  case MX_STATUS_ENDPOINT_CLOSED:
    return OMX_REMOTE_ENDPOINT_CLOSED;
  case MX_STATUS_ENDPOINT_UNREACHABLE:
    return OMX_REMOTE_ENDPOINT_UNREACHABLE;
  case MX_STATUS_BAD_SESSION:
    return OMX_REMOTE_ENDPOINT_BAD_SESSION;
  case MX_STATUS_BAD_KEY:
    return OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY;
  case MX_STATUS_BAD_ENDPOINT:
    return OMX_REMOTE_ENDPOINT_BAD_ID;
  case MX_STATUS_BAD_RDMAWIN:
    return OMX_REMOTE_RDMA_WINDOW_BAD_ID;
  case MX_STATUS_ABORTED:
    return OMX_MESSAGE_ABORTED;
  case MX_STATUS_NO_RESOURCES:
    return OMX_NO_RESOURCES;
  default:
    omx__abort(NULL, "Unexpected MX status code %d to translate into Open-MX\n", mxcode);
  }
}



/*************
 * MX symbols
 */

mx_return_t
mx__init_api(int api)
{
  omx_return_t omxret;
  omxret = omx__init_api(api);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_finalize(void)
{
  omx_return_t omxret;
  omxret = omx_finalize();
  return omx_return_to_mx(omxret);
}

static mx_error_handler_t omx__mx_error_handler = NULL;

static omx_return_t
omx__mx_error_handler_wrapper(char *buffer, omx_return_t ret)
{
  omx__debug_assert(omx__mx_error_handler != NULL);
  return omx__mx_error_handler(buffer, ret);
}

mx_error_handler_t
mx_set_error_handler(mx_error_handler_t new_mxhdlr)
{
  mx_error_handler_t old_mxhldr;
  omx_error_handler_t old_omxhdlr;

  old_mxhldr = omx__mx_error_handler;
  omx__mx_error_handler = new_mxhdlr;
  old_omxhdlr = omx_set_error_handler(NULL, omx__mx_error_handler_wrapper);

  /* if there was a MX-specific handler, return it. otherwise return the default Open-MX handler */
  return omx__mx_error_handler ? omx__mx_error_handler : omx_error_handler_to_mx(old_omxhdlr);
}

mx_return_t
mx__errors_are_fatal(char *str, mx_return_t ret)
{
  omx_return_t omxret;
  omxret = omx_return_to_mx(OMX_ERRORS_ARE_FATAL(str, omx_return_from_mx(ret)));
  return omx_return_to_mx(omxret);
}
const mx_error_handler_t MX_ERRORS_ARE_FATAL = mx__errors_are_fatal;

mx_return_t
mx__errors_return(char *str, mx_return_t ret)
{
  omx_return_t omxret;
  omxret = omx_return_to_mx(OMX_ERRORS_RETURN(str, omx_return_from_mx(ret)));
  return omx_return_to_mx(omxret);
}
const mx_error_handler_t MX_ERRORS_RETURN = mx__errors_return;

mx_return_t
mx_open_endpoint(uint32_t board_number, uint32_t endpoint_id,
		 uint32_t endpoint_key, mx_param_t *params_array, uint32_t params_count,
		 mx_endpoint_t *endpoint)
{
  omx_return_t omxret;

  /* check various constants */
  BUILD_BUG_ON(MX_ANY_NIC != OMX_ANY_NIC);
  BUILD_BUG_ON(MX_ANY_ENDPOINT != OMX_ANY_ENDPOINT);
  /* check endpoint parameter keys */
  BUILD_BUG_ON(MX_PARAM_ERROR_HANDLER != OMX_ENDPOINT_PARAM_ERROR_HANDLER);
  BUILD_BUG_ON(MX_PARAM_UNEXP_QUEUE_MAX != OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX);
  BUILD_BUG_ON(MX_PARAM_CONTEXT_ID != OMX_ENDPOINT_PARAM_CONTEXT_ID);

  omxret = omx_open_endpoint(board_number, endpoint_id, endpoint_key,
			     omx_endpoint_param_ptr_from_mx(params_array), params_count,
			     omx_endpoint_ptr_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_close_endpoint(mx_endpoint_t endpoint)
{
  omx_return_t omxret;
  omxret = omx_close_endpoint(omx_endpoint_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_wakeup(mx_endpoint_t endpoint)
{
  omx_return_t omxret;
  omxret = omx_wakeup(omx_endpoint_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_register_unexp_handler(mx_endpoint_t endpoint, mx_unexp_handler_t handler, void *context)
{
  omx_return_t omxret;

  /* check unexp handler return values */
  BUILD_BUG_ON(MX_RECV_CONTINUE != OMX_UNEXP_HANDLER_RECV_CONTINUE);
  BUILD_BUG_ON(MX_RECV_FINISHED != OMX_UNEXP_HANDLER_RECV_FINISHED);

  omxret = omx_register_unexp_handler(omx_endpoint_from_mx(endpoint),
				      omx_unexp_handler_from_mx(handler),
				      context);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_disable_progression(mx_endpoint_t endpoint)
{
  omx_return_t omxret;
  omxret = omx_disable_progression(omx_endpoint_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_reenable_progression(mx_endpoint_t endpoint)
{
  omx_return_t omxret;
  omxret = omx_reenable_progression(omx_endpoint_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_progress(mx_endpoint_t endpoint)
{
  omx_return_t omxret;
  omxret = omx_progress(omx_endpoint_from_mx(endpoint));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_isend(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
	 mx_endpoint_addr_t dest_endpoint, uint64_t match_info, void *context,
	 mx_request_t *request)
{
  omx_return_t omxret;

  /* check the contents of segment types, since their fields are different */
  BUILD_BUG_ON(sizeof(mx_segment_t) != sizeof(omx_seg_t));
  BUILD_BUG_ON(offsetof(mx_segment_t, segment_ptr) != offsetof(omx_seg_t, ptr));
  BUILD_BUG_ON(sizeof(((mx_segment_t*)NULL)->segment_ptr) != sizeof(((omx_seg_t*)NULL)->ptr));
  BUILD_BUG_ON(offsetof(mx_segment_t, segment_length) != offsetof(omx_seg_t, len));
  BUILD_BUG_ON(sizeof(((mx_segment_t*)NULL)->segment_length) != sizeof(((omx_seg_t*)NULL)->len));

  omxret = omx_isendv(omx_endpoint_from_mx(endpoint),
		      omx_seg_ptr_from_mx(segments_list), segments_count,
		      omx_endpoint_addr_from_mx(dest_endpoint),
		      match_info, context,
		      omx_request_ptr_from_mx(request));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_issend(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
	  mx_endpoint_addr_t dest_endpoint, uint64_t match_info, void *context,
	  mx_request_t *request)
{
  omx_return_t omxret;
  omxret = omx_issendv(omx_endpoint_from_mx(endpoint),
		       omx_seg_ptr_from_mx(segments_list), segments_count,
		       omx_endpoint_addr_from_mx(dest_endpoint),
		       match_info, context,
		       omx_request_ptr_from_mx(request));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_irecv(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
	 uint64_t match_info, uint64_t match_mask, void *context,
	 mx_request_t *request)
{
  omx_return_t omxret;
  omxret = omx_irecvv(omx_endpoint_from_mx(endpoint),
		      omx_seg_ptr_from_mx(segments_list), segments_count,
		      match_info, match_mask, context,
		      omx_request_ptr_from_mx(request));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_cancel(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result)
{
  omx_return_t omxret;
  omxret = omx_cancel(omx_endpoint_from_mx(endpoint),
		      omx_request_ptr_from_mx(request),
		      result);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_forget(mx_endpoint_t endpoint, mx_request_t *request)
{
  omx_return_t omxret;
  omxret = omx_forget(omx_endpoint_from_mx(endpoint),
		      omx_request_ptr_from_mx(request));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_test(mx_endpoint_t endpoint, mx_request_t * request,
	mx_status_t *mxstatus, uint32_t * result)
{
  omx_return_t omxret;
  omx_status_t omxstatus;
  omxret = omx_test(omx_endpoint_from_mx(endpoint),
		    omx_request_ptr_from_mx(request),
		    &omxstatus, result);
  if (omxret == OMX_SUCCESS && *result)
    omx_status_to_mx(mxstatus, &omxstatus);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_wait(mx_endpoint_t endpoint, mx_request_t *request,
	uint32_t timeout, mx_status_t *mxstatus, uint32_t *result)
{
  omx_return_t omxret;
  omx_status_t omxstatus;
  omxret = omx_wait(omx_endpoint_from_mx(endpoint),
		    omx_request_ptr_from_mx(request),
		    &omxstatus, result,
		    omx_timeout_from_mx(timeout));
  if (omxret == OMX_SUCCESS && *result)
    omx_status_to_mx(mxstatus, &omxstatus);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_test_any(mx_endpoint_t endpoint, uint64_t match_info, uint64_t match_mask,
	    mx_status_t *mxstatus, uint32_t *result)
{
  omx_return_t omxret;
  omx_status_t omxstatus;
  omxret = omx_test_any(omx_endpoint_from_mx(endpoint),
			match_info, match_mask,
			&omxstatus, result);
  if (omxret == OMX_SUCCESS && *result)
    omx_status_to_mx(mxstatus, &omxstatus);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_wait_any(mx_endpoint_t endpoint, uint32_t timeout, uint64_t match_info, uint64_t match_mask,
	    mx_status_t *mxstatus, uint32_t *result)
{
  omx_return_t omxret;
  omx_status_t omxstatus;
  omxret = omx_wait_any(omx_endpoint_from_mx(endpoint),
			match_info, match_mask,
			&omxstatus, result,
			omx_timeout_from_mx(timeout));
  if (omxret == OMX_SUCCESS && *result)
    omx_status_to_mx(mxstatus, &omxstatus);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_ipeek(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result)
{
  omx_return_t omxret;
  omxret = omx_ipeek(omx_endpoint_from_mx(endpoint),
		     omx_request_ptr_from_mx(request),
		     result);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_peek(mx_endpoint_t endpoint, uint32_t timeout, mx_request_t *request, uint32_t *result)
{
  omx_return_t omxret;
  omxret = omx_peek(omx_endpoint_from_mx(endpoint),
		    omx_request_ptr_from_mx(request),
		    result,
		    omx_timeout_from_mx(timeout));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_iprobe(mx_endpoint_t endpoint, uint64_t match_info, uint64_t match_mask,
	  mx_status_t *mxstatus, uint32_t *result)
{
  omx_return_t omxret;
  omx_status_t omxstatus;
  omxret = omx_iprobe(omx_endpoint_from_mx(endpoint),
		      match_info, match_mask,
		      &omxstatus, result);
  if (omxret == OMX_SUCCESS && *result)
    omx_status_to_mx(mxstatus, &omxstatus);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_probe(mx_endpoint_t endpoint, uint32_t timeout, uint64_t match_info, uint64_t match_mask,
	 mx_status_t *mxstatus, uint32_t *result)
{
  omx_return_t omxret;
  omx_status_t omxstatus;
  omxret = omx_probe(omx_endpoint_from_mx(endpoint),
		     match_info, match_mask,
		     &omxstatus, result,
		     omx_timeout_from_mx(timeout));
  if (omxret == OMX_SUCCESS && *result)
    omx_status_to_mx(mxstatus, &omxstatus);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_ibuffered(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result)
{
  omx_return_t omxret;
  omxret = omx_ibuffered(omx_endpoint_from_mx(endpoint),
			 omx_request_ptr_from_mx(request),
			 result);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_context(mx_request_t *request, void **context)
{
  omx_return_t omxret;
  omxret = omx_context(omx_request_ptr_from_mx(request),
		       context);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_get_info(mx_endpoint_t mx_endpoint, mx_get_info_key_t key,
	    void *in_val, uint32_t in_len,
	    void *out_val, uint32_t out_len)
{
  omx_endpoint_t omx_ep = omx_endpoint_from_mx(mx_endpoint);

  switch (key) {
  case MX_NIC_COUNT:
    return omx_return_to_mx(omx_get_info(omx_ep, OMX_INFO_BOARD_COUNT,
					 in_val, in_len, out_val, out_len));

  case MX_NIC_IDS:
    return omx_return_to_mx(omx_get_info(omx_ep, OMX_INFO_BOARD_IDS,
					 in_val, in_len, out_val, out_len));

  case MX_MAX_NATIVE_ENDPOINTS:
    return omx_return_to_mx(omx_get_info(omx_ep, OMX_INFO_ENDPOINT_MAX,
					 in_val, in_len, out_val, out_len));

  case MX_NATIVE_REQUESTS:
    * (uint32_t *) out_val = UINT32_MAX;
    return MX_SUCCESS;

  case MX_COUNTERS_COUNT:
    return omx_return_to_mx(omx_get_info(omx_ep, OMX_INFO_COUNTER_MAX,
					 in_val, in_len, out_val, out_len));

  case MX_COUNTERS_LABELS: {
    omx_return_t ret;
    uint32_t count,i;

    ret = omx_get_info(omx_ep, OMX_INFO_COUNTER_MAX,
		       NULL, 0, &count, sizeof(count));
    if (ret != OMX_SUCCESS)
      return omx_return_to_mx(ret);

    if (out_len < count * MX_MAX_STR_LEN) {
      if (omx_ep)
	return omx_return_to_mx(omx__error_with_ep(omx_ep, MX_BAD_INFO_LENGTH,
						   "Copying counters labels (%ld bytes into %ld)",
						   (unsigned long) out_len, (unsigned long)(count * MX_MAX_STR_LEN)));
      else
	return omx_return_to_mx(omx__error(MX_BAD_INFO_LENGTH,
					   "Copying counters labels (%ld bytes into %ld)",
					   (unsigned long) out_len, (unsigned long)(count * MX_MAX_STR_LEN)));
    }

    for(i=0; i<count; i++)
      omx_get_info(omx_ep, OMX_INFO_COUNTER_LABEL,
		   NULL, 0, &((char *) out_val)[i*MX_MAX_STR_LEN], MX_MAX_STR_LEN);

    return MX_SUCCESS;
  }

  case MX_COUNTERS_VALUES:
    return omx_return_to_mx(omx_get_info(omx_ep, OMX_INFO_COUNTER_VALUES,
					 in_val, in_len, out_val, out_len));

  case MX_PRODUCT_CODE:
  case MX_PART_NUMBER:
  case MX_SERIAL_NUMBER:
    strncpy((char*)out_val, "N/A (Open-MX)", out_len-1);
    ((char*)out_val)[out_len-1] = '\0';
    return MX_SUCCESS;

  case MX_PORT_COUNT:
    * (uint32_t *) out_val = 1; /* can we know more from the driver? */
    return MX_SUCCESS;

  case MX_PIO_SEND_MAX:
    * (uint32_t *) out_val = OMX_SMALL_MSG_LENGTH_MAX;
    return MX_SUCCESS;

  case MX_COPY_SEND_MAX:
    * (uint32_t *) out_val = OMX_MEDIUM_MSG_LENGTH_MAX;
    return MX_SUCCESS;

  case MX_NUMA_NODE:
    return omx_return_to_mx(omx_get_info(omx_ep, OMX_INFO_BOARD_NUMA_NODE,
					 in_val, in_len, out_val, out_len));

  case MX_NET_TYPE:
    * (uint32_t *) out_val = MX_NET_ETHER;
    return MX_SUCCESS;

  case MX_LINE_SPEED:
    * (uint32_t *) out_val = MX_SPEED_OPEN_MX;
    return MX_SUCCESS;

  }

  if (omx_ep)
    return omx_return_to_mx(omx__error_with_ep(omx_ep, MX_BAD_INFO_KEY,
					       "Getting info with key %ld",
					       (unsigned long) key));
  else
    return omx_return_to_mx(omx__error(MX_BAD_INFO_KEY,
				       "Getting info with key %ld",
				       (unsigned long) key));
}

mx_return_t
mx_hostname_to_nic_id(char *hostname, uint64_t *nic_id)
{
  omx_return_t omxret;
  omxret = omx_hostname_to_nic_id(hostname, nic_id);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_board_number_to_nic_id(uint32_t board_number, uint64_t *nic_id)
{
  omx_return_t omxret;
  omxret = omx_board_number_to_nic_id(board_number, nic_id);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_nic_id_to_board_number(uint64_t nic_id, uint32_t *board_number)
{
  omx_return_t omxret;
  omxret = omx_nic_id_to_board_number(nic_id, board_number);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_nic_id_to_hostname(uint64_t nic_id, char *hostname)
{
  omx_return_t omxret;
  omxret = omx_nic_id_to_hostname(nic_id, hostname);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_connect(mx_endpoint_t endpoint, uint64_t nic_id, uint32_t endpoint_id,
	   uint32_t key, uint32_t timeout, mx_endpoint_addr_t *addr)
{
  omx_return_t omxret;
  omxret = omx_connect(omx_endpoint_from_mx(endpoint),
		       nic_id, endpoint_id, key,
		       omx_timeout_from_mx(timeout),
		       omx_endpoint_addr_ptr_from_mx(addr));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_iconnect(mx_endpoint_t endpoint, uint64_t nic_id, uint32_t eid, uint32_t key,
	    uint64_t match_info, void *context, mx_request_t *request)
{
  omx_return_t omxret;
  omxret = omx_iconnect(omx_endpoint_from_mx(endpoint),
			nic_id, eid, key, match_info,
			context,
			omx_request_ptr_from_mx(request));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_disconnect(mx_endpoint_t endpoint, mx_endpoint_addr_t addr)
{
  omx_return_t omxret;
  omxret = omx_disconnect(omx_endpoint_from_mx(endpoint),
			  omx_endpoint_addr_from_mx(addr));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_set_request_timeout(mx_endpoint_t endpoint, mx_request_t request, uint32_t milli_seconds)
{
  omx_return_t omxret;
  omxret = omx_set_request_timeout(omx_endpoint_from_mx(endpoint),
				   omx_request_from_mx(request),
				   milli_seconds);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_decompose_endpoint_addr(mx_endpoint_addr_t endpoint_addr,
			   uint64_t *nic_id, uint32_t *endpoint_id)
{
  omx_return_t omxret;
  omxret = omx_decompose_endpoint_addr(omx_endpoint_addr_from_mx(endpoint_addr),
				       nic_id, endpoint_id);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_decompose_endpoint_addr2(mx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id, uint32_t *session_id)
{
  omx_return_t omxret;
  omxret = omx_decompose_endpoint_addr_with_session(omx_endpoint_addr_from_mx(endpoint_addr),
						    nic_id, endpoint_id, session_id);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_get_endpoint_addr(mx_endpoint_t endpoint, mx_endpoint_addr_t *endpoint_addr)
{
  omx_return_t omxret;

  /* check various constants */
  BUILD_BUG_ON(MX_SIZEOF_ADDR != OMX_SIZEOF_ADDR);

  omxret = omx_get_endpoint_addr(omx_endpoint_from_mx(endpoint),
				 omx_endpoint_addr_ptr_from_mx(endpoint_addr));
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_set_endpoint_addr_context(mx_endpoint_addr_t endpoint_addr, void *context)
{
  omx_return_t omxret;
  omxret = omx_set_endpoint_addr_context(omx_endpoint_addr_from_mx(endpoint_addr),
					 context);
  return omx_return_to_mx(omxret);
}

mx_return_t
mx_get_endpoint_addr_context(mx_endpoint_addr_t endpoint_addr, void **context)
{
  omx_return_t omxret;
  omxret = omx_get_endpoint_addr_context(omx_endpoint_addr_from_mx(endpoint_addr),
					 context);
  return omx_return_to_mx(omxret);
}

const char *
mx_strerror(mx_return_t mxret)
{
  return omx_strerror(omx_return_from_mx(mxret));
}

const char *
mx_strstatus(mx_status_code_t mxcode)
{
  return omx_strerror(omx_status_code_from_mx(mxcode));
}

/*
 * Not implemented yet
 */

mx_return_t
mx_register_unexp_callback(mx_endpoint_t endpoint, mx_matching_callback_t cb, void *ctxt)
{
  omx__abort(NULL, "mx_register_unexp_callback not implemented since it's deprecated by mx_register_unexp_handler\n");
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_iput(mx_endpoint_t endpoint, void *local_addr, uint32_t length,
	mx_endpoint_addr_t dest_endpoint, uint64_t remote_addr, void *context,
	mx_request_t *request)
{
  omx__abort(NULL, "mx_iput not implemented\n");
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_iget(mx_endpoint_t endpoint, void *local_addr, uint32_t length,
	mx_endpoint_addr_t dest_endpoint, uint64_t remote_addr, void *context,
	mx_request_t *request)
{
  omx__abort(NULL, "mx_iget not implemented\n");
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_buffered(mx_endpoint_t endpoint, mx_request_t *request, uint32_t timeout, uint32_t *result)
{
  omx__abort(NULL, "mx_buffered not implemented since it is not in MX either\n");
  return MX_BAD_BAD_BAD;
}

/****************************************
 * Internal MX symbols (for OpenMPI 1.3)
 */

mx_return_t
mx_open_board(int i, mx_endpt_handle_t *handle)
{
  return MX_SUCCESS;
}

typedef struct {
  uint32_t board_number;
  uint8_t mapper_mac[6];
  uint16_t iport;
  uint32_t map_version;
  uint32_t num_hosts;
  uint32_t network_configured;
  uint32_t routes_valid;
  uint32_t level;
  uint32_t flags;
} mx_mapper_state_t;

mx_return_t
mx__get_mapper_state(mx_endpt_handle_t handle, mx_mapper_state_t *p)
{
  memset(&p->mapper_mac, 0, sizeof(p->mapper_mac));
  return MX_SUCCESS;
}

int
mx__regcache_clean(void *ptr, size_t len)
{
  return 0;
}
