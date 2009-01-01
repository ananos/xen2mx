/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#include "open-mx.h"
#include "stdio.h"
#include "assert.h"

static int discard = 0;

static omx_unexp_handler_action_t
unexp_handler(void *context, omx_endpoint_addr_t source,
	      uint64_t match_info, uint32_t msg_length,
	      void * data_if_available)
{
  omx_endpoint_t ep = context;
  omx_return_t ret;

  if (discard) {
    printf("handler discarding directly\n");
    return OMX_UNEXP_HANDLER_RECV_FINISHED;
  } else {
    printf("handler discarding through a forgotten receive\n");
    ret = omx_irecv(ep, NULL, 0, 0, 0, NULL, NULL);
    return OMX_UNEXP_HANDLER_RECV_CONTINUE;
  }
}

int
main(int argc, char *argv[])
{
  omx_return_t ret;
  omx_endpoint_t ep;
  omx_endpoint_addr_t addr;
  omx_request_t req;
  omx_status_t status;
  uint32_t result;

  putenv("OMX_DISABLE_SELF=1");
  putenv("OMX_DISABLE_SHARED=1");

  ret = omx_init();
  assert(ret == OMX_SUCCESS);

  ret = omx_open_endpoint(OMX_ANY_NIC, OMX_ANY_ENDPOINT, 0x12345678, NULL, 0, &ep);
  assert(ret == OMX_SUCCESS);

  ret = omx_get_endpoint_addr(ep, &addr);
  assert(ret == OMX_SUCCESS);

  ret = omx_register_unexp_handler(ep, &unexp_handler, ep);
  assert(ret == OMX_SUCCESS);

  printf("posting isend\n");
  ret = omx_isend(ep, NULL, 0, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);
  printf("isend completed\n");

  printf("posting issend\n");
  ret = omx_issend(ep, NULL, 0, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);
  printf("issend completed\n");

  printf("switching to unexpected handler discarding directly\n");
  discard = 1;

  printf("posting isend\n");
  ret = omx_isend(ep, NULL, 0, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);
  printf("isend completed\n");

  printf("posting issend\n");
  ret = omx_issend(ep, NULL, 0, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);
  printf("issend completed\n");

  omx_close_endpoint(ep);
  omx_finalize();
  return 0;
}
