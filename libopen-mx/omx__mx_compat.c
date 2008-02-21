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
 * enabled in order to expose MX symbols names since they may be required.
 * Known cases include:
 * + MPICH-MX using &mx_test
 * + Open MPI configure script looking for mx_finalize in libmyriexpress.so
 */

#include "open-mx.h"
#define OMX_NO_FUNC_WRAPPERS
#include "myriexpress.h"

#include "omx_lib.h"

mx_return_t
mx__init_api(int api)
{
  return omx__init_api(api);
}

void
mx_finalize(void)
{
  omx_finalize();
}

mx_return_t
mx_open_endpoint(uint32_t board_number, uint32_t endpoint_id,
		 uint32_t endpoint_key, mx_param_t *params_array, uint32_t params_count,
		 mx_endpoint_t *endpoint)
{
  return omx_open_endpoint(board_number, endpoint_id, endpoint_key, params_array, params_count, endpoint);
}

mx_return_t
mx_close_endpoint(mx_endpoint_t endpoint)
{
  return omx_close_endpoint(endpoint);
}

mx_return_t
mx_wakeup(mx_endpoint_t endpoint)
{
  return omx_wakeup(endpoint);
}

mx_return_t
mx_disable_progression(mx_endpoint_t ep)
{
  return omx_disable_progression(ep);
}

mx_return_t
mx_reenable_progression(mx_endpoint_t ep)
{
  return omx_reenable_progression(ep);
}

mx_return_t
mx_progress(mx_endpoint_t ep)
{
  return omx_progress(ep);
}

mx_return_t
mx_isend(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
	 mx_endpoint_addr_t dest_endpoint, uint64_t match_info, void *context,
	 mx_request_t *request)
{
  return omx_isendv(endpoint, (struct omx_seg *) (void *) segments_list, segments_count, dest_endpoint, match_info, context, request);
}

mx_return_t
mx_issend(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
	  mx_endpoint_addr_t dest_endpoint, uint64_t match_info, void *context,
	  mx_request_t *request)
{
  return omx_issendv(endpoint, (struct omx_seg *) (void *) segments_list, segments_count, dest_endpoint, match_info, context, request);
}

mx_return_t
mx_irecv(mx_endpoint_t endpoint, mx_segment_t *segments_list, uint32_t segments_count,
	 uint64_t match_info, uint64_t match_mask, void *context,
	 mx_request_t *request)
{
  return omx_irecvv(endpoint, (struct omx_seg *) (void *) segments_list, segments_count, match_info, match_mask, context, request);
}

mx_return_t
mx_cancel(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result)
{
  return omx_cancel(endpoint, request, result);
}

mx_return_t
mx_forget(mx_endpoint_t endpoint, mx_request_t *request)
{
  return omx_forget(endpoint, request);
}

mx_return_t
mx_test(mx_endpoint_t ep, mx_request_t * request,
	mx_status_t *status, uint32_t * result)
{
  return omx_test(ep, request, (struct omx_status *) (void *) status, result);
}

mx_return_t
mx_wait(mx_endpoint_t endpoint, mx_request_t *request,
	uint32_t timeout, mx_status_t *status, uint32_t *result)
{
  return omx_wait(endpoint, request, (struct omx_status *) (void *) status, result, timeout);
}

mx_return_t
mx_test_any(mx_endpoint_t endpoint, uint64_t match_info, uint64_t match_mask,
	    mx_status_t *status, uint32_t *result)
{
  return omx_test_any(endpoint, match_info, match_mask, (struct omx_status *) (void *) status, result);
}

mx_return_t
mx_wait_any(mx_endpoint_t endpoint, uint32_t timeout, uint64_t match_info, uint64_t match_mask,
	    mx_status_t *status, uint32_t *result)
{
  return omx_wait_any(endpoint, match_info, match_mask, (struct omx_status *) (void *) status, result, timeout);
}

mx_return_t
mx_ipeek(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result)
{
  return omx_ipeek(endpoint, request, result);
}

mx_return_t
mx_peek(mx_endpoint_t endpoint, uint32_t timeout, mx_request_t *request, uint32_t *result)
{
  return omx_peek(endpoint, request, result, timeout);
}

mx_return_t
mx_iprobe(mx_endpoint_t endpoint, uint64_t match_info, uint64_t match_mask,
	  mx_status_t *status, uint32_t *result)
{
  return omx_iprobe(endpoint, match_info, match_mask, (struct omx_status *) (void *) status, result);
}

mx_return_t
mx_probe(mx_endpoint_t endpoint, uint32_t timeout, uint64_t match_info, uint64_t match_mask,
	 mx_status_t *status, uint32_t *result)
{
  return omx_probe(endpoint, match_info, match_mask, (struct omx_status *) (void *) status, result, timeout);
}

mx_return_t
mx_ibuffered(mx_endpoint_t endpoint, mx_request_t *request, uint32_t *result)
{
  return omx_ibuffered(endpoint, request, result);
}

mx_return_t
mx_context(mx_request_t *request, void **context)
{
  return omx_context(request, context);
}

mx_return_t
mx_get_info(mx_endpoint_t ep, mx_get_info_key_t key,
	    void *in_val, uint32_t in_len,
	    void *out_val, uint32_t out_len)
{
  switch (key) {
  case MX_NIC_COUNT:
    return omx_get_info(ep, OMX_INFO_BOARD_COUNT, in_val, in_len, out_val, out_len);

  case MX_NIC_IDS:
    return omx_get_info(ep, OMX_INFO_BOARD_IDS, in_val, in_len, out_val, out_len);

  case MX_MAX_NATIVE_ENDPOINTS:
    return omx_get_info(ep, OMX_INFO_ENDPOINT_MAX, in_val, in_len, out_val, out_len);

  case MX_NATIVE_REQUESTS:
    * (uint32_t *) out_val = UINT32_MAX;
    return MX_SUCCESS;

  case MX_COUNTERS_COUNT:
    return omx_get_info(ep, OMX_INFO_COUNTER_MAX, in_val, in_len, out_val, out_len);

  case MX_COUNTERS_LABELS: {
    omx_return_t ret;
    uint32_t count,i;

    ret = omx_get_info(ep, OMX_INFO_COUNTER_MAX, NULL, 0, &count, sizeof(count));
    if (ret != OMX_SUCCESS)
      return ret;

    if (out_len < count * MX_MAX_STR_LEN)
      return MX_BAD_INFO_LENGTH;

    for(i=0; i<count; i++)
      omx_get_info(ep, OMX_INFO_COUNTER_LABEL, NULL, 0, &((char *) out_val)[i*MX_MAX_STR_LEN], MX_MAX_STR_LEN);

    return MX_SUCCESS;
  }

  case MX_COUNTERS_VALUES:
    return omx_get_info(ep, OMX_INFO_COUNTER_VALUES, in_val, in_len, out_val, out_len);

  case MX_PRODUCT_CODE:
  case MX_PART_NUMBER:
  case MX_SERIAL_NUMBER:
    if (out_len < MX_MAX_STR_LEN)
      return MX_BAD_INFO_LENGTH;
    strcpy((char*)out_val, "N/A (Open-MX)");
    return MX_SUCCESS;

  case MX_PORT_COUNT:
    * (uint32_t *) out_val = 1; /* can we know more from the driver? */
    return MX_SUCCESS;

  case MX_PIO_SEND_MAX:
    * (uint32_t *) out_val = OMX_SMALL_MAX;
    return MX_SUCCESS;

  case MX_COPY_SEND_MAX:
    * (uint32_t *) out_val = OMX_MEDIUM_MAX;
    return MX_SUCCESS;

  case MX_NUMA_NODE:
    return omx_get_info(ep, OMX_INFO_BOARD_NUMA_NODE, in_val, in_len, out_val, out_len);

  case MX_NET_TYPE:
    * (uint32_t *) out_val = MX_NET_ETHER;
    return MX_SUCCESS;

  case MX_LINE_SPEED:
    * (uint32_t *) out_val = MX_SPEED_OPEN_MX;
    return MX_SUCCESS;

  }

  return MX_BAD_INFO_KEY;
}

mx_return_t
mx_hostname_to_nic_id(char *hostname, uint64_t *nic_id)
{
  return omx_hostname_to_nic_id(hostname, nic_id);
}

mx_return_t
mx_board_number_to_nic_id(uint32_t board_number, uint64_t *nic_id)
{
  return omx_board_number_to_nic_id(board_number, nic_id);
}

mx_return_t
mx_nic_id_to_board_number(uint64_t nic_id, uint32_t *board_number)
{
  return omx_nic_id_to_board_number(nic_id, board_number);
}

mx_return_t
mx_nic_id_to_hostname(uint64_t nic_id, char *hostname)
{
  return omx_nic_id_to_hostname(nic_id, hostname);
}

mx_return_t
mx_connect(mx_endpoint_t endpoint, uint64_t nic_id, uint32_t endpoint_id,
	   uint32_t key, uint32_t timeout, mx_endpoint_addr_t *addr)
{
  return omx_connect(endpoint, nic_id, endpoint_id, key, timeout, addr);
}

mx_return_t
mx_iconnect(mx_endpoint_t ep, uint64_t nic_id, uint32_t eid, uint32_t key,
	    uint64_t match_info, void *context, mx_request_t *request)
{
  return omx_iconnect(ep, nic_id, eid, key, match_info, context, request);
}

mx_return_t
mx_disconnect(mx_endpoint_t ep, mx_endpoint_addr_t addr)
{
  return omx_disconnect(ep, addr);
}

mx_return_t
mx_set_request_timeout(mx_endpoint_t endpoint, mx_request_t request, uint32_t milli_seconds)
{
  return omx_set_request_timeout(endpoint, request, milli_seconds);
}

mx_return_t
mx_decompose_endpoint_addr(mx_endpoint_addr_t endpoint_addr,
			   uint64_t *nic_id, uint32_t *endpoint_id)
{
  return omx_decompose_endpoint_addr(endpoint_addr, nic_id, endpoint_id);
}

mx_return_t
mx_get_endpoint_addr(mx_endpoint_t endpoint, mx_endpoint_addr_t *endpoint_addr)
{
  return omx_get_endpoint_addr(endpoint, endpoint_addr);
}

mx_return_t
mx_set_endpoint_addr_context(mx_endpoint_addr_t endpoint_addr, void *context)
{
  return omx_set_endpoint_addr_context(endpoint_addr, context);
}

mx_return_t
mx_get_endpoint_addr_context(mx_endpoint_addr_t endpoint_addr, void **context)
{
  return omx_get_endpoint_addr_context(endpoint_addr, context);
}

const char *
mx_strerror(mx_return_t return_code)
{
  return omx_strerror(return_code);
}

const char *
mx_strstatus(mx_status_code_t status)
{
  return omx_strstatus(status);
}

#ifdef OMX_MX_API_UNSUPPORTED_COMPAT
/*
 * Not implemented yet
 */

mx_return_t
mx_register_unexp_callback(mx_endpoint_t ep, mx_matching_callback_t cb, void *ctxt)
{
  omx__abort("mx_register_unexp_callback not implemented\n");
  /* FIXME */
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_iput(mx_endpoint_t endpoint, void *local_addr, uint32_t length,
	mx_endpoint_addr_t dest_endpoint, uint64_t remote_addr, void *context,
	mx_request_t *request)
{
  omx__abort("mx_iput not implemented\n");
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_iget(mx_endpoint_t endpoint, void *local_addr, uint32_t length,
	mx_endpoint_addr_t dest_endpoint, uint64_t remote_addr, void *context,
	mx_request_t *request)
{
  omx__abort("mx_iget not implemented\n");
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_buffered(mx_endpoint_t endpoint, mx_request_t *request, uint32_t timeout, uint32_t *result)
{
  omx__abort("mx_buffered not implemented since it is not in MX either\n");
  return MX_BAD_BAD_BAD;
}

mx_return_t
mx_decompose_endpoint_addr2(mx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id, uint32_t *session_id)
{
  omx__abort("mx_decompose_endpoint_addr2 not implemented\n");
  return MX_BAD_BAD_BAD;
}
#endif /* OMX_MX_API_UNSUPPORTED_COMPAT */
