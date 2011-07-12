/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2010 (see AUTHORS file)
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

#define _BSD_SOURCE 1 /* for strdup */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>

#include "open-mx.h"

#define EID 0
#define RID 0

static void
crapify_heap(omx_endpoint_t ep)
{
  /* post some requests to crapify the next stuff in the internal heap (dlmalloc),
   * the OMX lib will likely use this memory later for allocating partners */
#define N 16
  omx_request_t req[N];
  omx_return_t ret;
  uint32_t result;
  int i;
  for(i=0; i<N; i++) {
    ret = omx_irecv(ep, NULL, 0, ~0ULL, ~0ULL, (void*) 0x12345678, &req[i]);
    if (ret != OMX_SUCCESS)
      break;
  }
  i--;
  for(; i>=0; i--)
    omx_cancel(ep, &req[i], &result);
}

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -d <hostname>\tset remote peer name and switch to sender mode\n");
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
}

int main(int argc, char *argv[])
{
  omx_return_t ret;
  int c;
  char *dest_hostname = NULL;
  omx_endpoint_t ep;
  omx_endpoint_addr_t myaddr, dest_addr;
  omx_request_t req;
  omx_status_t status;
  uint64_t dest_nicid;
  uint32_t dest_eid;
  uint32_t result;
  void *ctx;
  uint32_t eid = EID;
  uint32_t rid = RID;

  while ((c = getopt(argc, argv, "e:d:r:h")) != -1)
    switch (c) {
    case 'd':
      dest_hostname = strdup(optarg);
      eid = OMX_ANY_ENDPOINT;
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'r':
      rid = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  ret = omx_open_endpoint(OMX_ANY_NIC, eid, 0x87654321, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  crapify_heap(ep);

  ret = omx_get_endpoint_addr(ep, &myaddr);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get endpoint addr (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  /* check myaddr context */
  ret = omx_get_endpoint_addr_context(myaddr, &ctx);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get my endpoint addr context (%s)\n",
	    omx_strerror(ret));
    goto out;
  }
  assert(ctx == NULL);

  /* set and check myaddr context */
  ret = omx_set_endpoint_addr_context(myaddr, (void *) 0xdeadbeef);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to set my endpoint addr context (%s)\n",
	    omx_strerror(ret));
    goto out;
  }
  ret = omx_get_endpoint_addr_context(myaddr, &ctx);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get my endpoint addr context (%s)\n",
	    omx_strerror(ret));
    goto out;
  }
  assert(ctx == (void *) 0xdeadbeef);

  if (dest_hostname) {
    ret = omx_hostname_to_nic_id(dest_hostname, &dest_nicid);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Cannot find peer name %s\n", dest_hostname);
      goto out;
    }

    ret = omx_connect(ep, dest_nicid, rid, 0x87654321, OMX_TIMEOUT_INFINITE, &dest_addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to connect to peer %s\n", dest_hostname);
      goto out;
    }

    /* check destaddr context */
    ret = omx_get_endpoint_addr_context(dest_addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == NULL);

    /* set and check destaddr context */
    ret = omx_set_endpoint_addr_context(dest_addr, (void *) 0xcacacaca);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to set dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    ret = omx_get_endpoint_addr_context(dest_addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == (void *) 0xcacacaca);

    ret = omx_issend(ep, NULL, 0, dest_addr, 0, NULL, &req);
    assert(ret == OMX_SUCCESS);
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    assert(ret == OMX_SUCCESS);
    assert(result);
    ret = omx_irecv(ep, NULL, 0, 0, 0, NULL, &req);
    assert(ret == OMX_SUCCESS);
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    assert(ret == OMX_SUCCESS);
    assert(result);

    /* check recv source context */
    ret = omx_get_endpoint_addr_context(status.addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == (void *) 0xcacacaca);

    /* recheck destaddr context */
    ret = omx_get_endpoint_addr_context(dest_addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == (void *) 0xcacacaca);

  } else {
    /* receiver */

    ret = omx_irecv(ep, NULL, 0, 0, 0, NULL, &req);
    assert(ret == OMX_SUCCESS);
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    assert(ret == OMX_SUCCESS);
    assert(result);

    /* check destaddr context */
    ret = omx_get_endpoint_addr_context(status.addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == NULL);

    /* set and check destaddr context */
    ret = omx_set_endpoint_addr_context(status.addr, (void *) 0x13131313);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to set dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    ret = omx_get_endpoint_addr_context(status.addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == (void *) 0x13131313);

    ret = omx_decompose_endpoint_addr(status.addr, &dest_nicid, &dest_eid);
    assert(ret == OMX_SUCCESS);
    ret = omx_connect(ep, dest_nicid, dest_eid, 0x87654321, OMX_TIMEOUT_INFINITE, &dest_addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to connect back to peer\n");
      goto out;
    }

    /* recheck destaddr context */
    ret = omx_get_endpoint_addr_context(status.addr, &ctx);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get dest endpoint addr context (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
    assert(ctx == (void *) 0x13131313);

    ret = omx_issend(ep, NULL, 0, dest_addr, 0, NULL, &req);
    assert(ret == OMX_SUCCESS);
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    assert(ret == OMX_SUCCESS);
    assert(result);
  }

  /* recheck destaddr context */
  ret = omx_get_endpoint_addr_context(myaddr, &ctx);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get my endpoint addr context (%s)\n",
	    omx_strerror(ret));
    goto out;
  }
  assert(ctx == (void *) 0xdeadbeef);

  free(dest_hostname);
  return 0;

 out:
  free(dest_hostname);
  return -1;
}
