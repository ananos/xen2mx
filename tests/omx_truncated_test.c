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

#define _SVID_SOURCE 1 /* for putenv */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "open-mx.h"

static void
one_length(omx_endpoint_t ep, omx_endpoint_addr_t addr, char *send_buffer, char *recv_buffer, unsigned length)
{
  omx_request_t sreq, rreq;
  omx_return_t ret;
  omx_status_t status;
  uint32_t result;
  int i;

  memset(recv_buffer, 0, length);

  printf("posting irecv %d\n", length/2);
  ret = omx_irecv(ep, recv_buffer, length/2, 0, 0, NULL, &rreq);
  assert(ret == OMX_SUCCESS);
  printf("posting isend %d\n", length);
  ret = omx_isend(ep, send_buffer, length, addr, 0, NULL, &sreq);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.msg_length = length);
  assert(status.xfer_length = length/2);
  ret = omx_wait(ep, &sreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.msg_length = length);
  if (length > 32768)
    assert(status.xfer_length = length/2);
  else
    assert(status.xfer_length = length);

  for(i=0; i<length/2; i++)
    assert(recv_buffer[i] == send_buffer[i]);
  for(i=length/2; i<length; i++)
    assert(recv_buffer[i] == 0);
}

int
main(int argc, char *argv[])
{
  omx_return_t ret;
  omx_endpoint_t ep;
  omx_endpoint_addr_t addr;
  char *send_buffer, *recv_buffer;
  int i;

  putenv("OMX_DISABLE_SELF=1");
  putenv("OMX_DISABLE_SHARED=1");

  send_buffer = malloc(1024*1024);
  assert(send_buffer);
  recv_buffer = malloc(1024*1024);
  assert(recv_buffer);
  for(i=0; i<1024*1024; i++)
    send_buffer[i] = 'a' + (i%26);

  ret = omx_init();
  assert(ret == OMX_SUCCESS);

  ret = omx_open_endpoint(OMX_ANY_NIC, OMX_ANY_ENDPOINT, 0x12345678, NULL, 0, &ep);
  assert(ret == OMX_SUCCESS);

  (void) omx_set_error_handler(ep, OMX_ERRORS_RETURN);

  ret = omx_get_endpoint_addr(ep, &addr);
  assert(ret == OMX_SUCCESS);

  one_length(ep, addr, send_buffer, recv_buffer, 10); /* tiny */
  one_length(ep, addr, send_buffer, recv_buffer, 120); /* small */
  one_length(ep, addr, send_buffer, recv_buffer, 20000); /* medium */
  one_length(ep, addr, send_buffer, recv_buffer, 1024*1024); /* large */

  omx_close_endpoint(ep);
  omx_finalize();
  return 0;
}
