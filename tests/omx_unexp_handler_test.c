/*
 * Open-MX
 * Copyright © INRIA 2007-2010 (see AUTHORS file)
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
#include <assert.h>

#include "open-mx.h"

static int discard;
static int sync;
static uint32_t length;

static omx_unexp_handler_action_t
unexp_handler(void *context, omx_endpoint_addr_t source,
	      uint64_t match_info, uint32_t msg_length,
	      void * data_if_available)
{
  omx_endpoint_t ep = context;

  /* check the message length */
  assert(msg_length == length);

  /* check that async message with a single frag have data available */
  if (sync || length > 4096)
    assert(!data_if_available);
  else
    assert(data_if_available);

  if (data_if_available) {
    unsigned i;
    for(i=0; i<msg_length; i++)
      assert(((char*)data_if_available)[i] == (char)('a' + (i%26)));
  }

  if (discard) {
    printf("handler discarding directly\n");
    return OMX_UNEXP_HANDLER_RECV_FINISHED;
  } else {
    printf("handler discarding through a forgotten receive\n");
    omx_irecv(ep, NULL, 0, 0, 0, NULL, NULL);
    return OMX_UNEXP_HANDLER_RECV_CONTINUE;
  }
}

static void
one_length(omx_endpoint_t ep, omx_endpoint_addr_t addr, char *buffer)
{
  omx_return_t ret;
  omx_request_t req;
  omx_status_t status;
  uint32_t result;

  printf("unexpected handler not discarding\n");
  discard = 0;

  sync = 0;
  printf("posting isend %d\n", length);
  ret = omx_isend(ep, buffer, length, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  if (length > 32768) /* large messages are sync and thus truncated by discarding */
    assert(status.code == OMX_MESSAGE_TRUNCATED);
  else
    assert(status.code == OMX_SUCCESS);
  printf("isend completed\n");

  sync = 1;
  printf("posting issend %d\n", length);
  ret = omx_issend(ep, buffer, length, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  if (length) /* non-empty sync messages truncated by 0-byte recv */
    assert(status.code == OMX_MESSAGE_TRUNCATED);
  else
    assert(status.code == OMX_SUCCESS);
  printf("issend completed\n");

  printf("unexpected handler discarding directly\n");
  discard = 1;

  sync = 0;
  printf("posting isend %d\n", length);
  ret = omx_isend(ep, buffer, length, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  if (length > 32768) /* large messages are sync and thus truncated by discarding */
    assert(status.code == OMX_MESSAGE_TRUNCATED);
  else
    assert(status.code == OMX_SUCCESS);
  printf("isend completed\n");

  sync = 1;
  printf("posting issend %d\n", length);
  ret = omx_issend(ep, buffer, length, addr, 0, NULL, &req);
  assert(ret == OMX_SUCCESS);
  printf("waiting for completion\n");
  ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  if (length) /* non-empty sync messages truncated by discarding */
    assert(status.code == OMX_MESSAGE_TRUNCATED);
  else
    assert(status.code == OMX_SUCCESS);
  printf("issend completed\n");
}

int
main(int argc, char *argv[])
{
  omx_return_t ret;
  omx_endpoint_t ep;
  omx_endpoint_addr_t addr;
  char *buffer;
  int i;

  putenv("OMX_DISABLE_SELF=1");
  putenv("OMX_DISABLE_SHARED=1");

  buffer = malloc(1024*1024);
  assert(buffer);
  for(i=0; i<4096; i++)
    buffer[i] = 'a' + (i%26);

  ret = omx_init();
  assert(ret == OMX_SUCCESS);

  ret = omx_open_endpoint(OMX_ANY_NIC, OMX_ANY_ENDPOINT, 0x12345678, NULL, 0, &ep);
  assert(ret == OMX_SUCCESS);

  (void) omx_set_error_handler(ep, OMX_ERRORS_RETURN);

  ret = omx_get_endpoint_addr(ep, &addr);
  assert(ret == OMX_SUCCESS);

  ret = omx_register_unexp_handler(ep, &unexp_handler, ep);
  assert(ret == OMX_SUCCESS);

  length = 0; /* empty, data not available */
  one_length(ep, addr, buffer);
  length = 16; /* tiny */
  one_length(ep, addr, buffer);
  length = 100; /* small */
  one_length(ep, addr, buffer);
  length = 1024; /* medium one frag */
  one_length(ep, addr, buffer);
  length = 10000; /* medium two frag, data not available */
  one_length(ep, addr, buffer);
  length = 1024*1024; /* large, data not available */
  one_length(ep, addr, buffer);

  omx_close_endpoint(ep);
  omx_finalize();
  return 0;
}
