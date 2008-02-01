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

void
mx_finalize(void)
{
  omx_finalize();
}

mx_return_t
mx_test(mx_endpoint_t ep, mx_request_t * request,
	mx_status_t *status, uint32_t * result)
{
  return omx_test(ep, request, (struct omx_status *) (void *) status, result);
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
