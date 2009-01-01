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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <sys/time.h>

#include "open-mx.h"

#define BID 0
#define EID 0
#define RID 0

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [opts]\n", argv[0]);
  fprintf(stderr, " -d <hostname>\t destination hostname, required for sender\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, "-h - help\n");
}

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  uint64_t nic_id;
  uint32_t bid = BID;
  uint32_t eid = EID;
  uint32_t rid = RID;
  uint32_t result;
  omx_status_t status;
  omx_seg_t segs[2] = { { NULL, 0 }, { NULL, 0 } };
  char * dest_hostname = NULL;
  omx_request_t req;
  omx_return_t ret;
  int c;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "hd:b:e:r:")) != EOF) switch(c) {
  case 'd':
    dest_hostname = optarg;
    ret = omx_hostname_to_nic_id(dest_hostname, &nic_id);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Cannot find peer name %s\n", argv[1]);
      goto out_with_ep;
    }
    break;
  case 'b':
    bid = atoi(optarg);
    break;
  case 'e':
    eid = atoi(optarg);
    break;
  case 'r':
    rid = atoi(optarg);
    break;
  case 'h':
  default:
    usage(argc, argv);
    exit(1);
  }

  ret = omx_open_endpoint(bid, eid, 0x12345678, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out_with_init;
  }

  /* If no host, we are receiver */
  if (dest_hostname == NULL) {
    printf("Starting omx_cancel_test dummy receiver, please ^Z me to test connect on the sender's side\n");
    sleep(10000);
    exit(0);
  }

  ret = omx_iconnect(ep, nic_id, rid, 0x12345678, 0, NULL, &req);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to iconnect (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  ret = omx_cancel(ep, &req, &result);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to cancel iconnect (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }
  if (result) {
    printf("successfully cancelled iconnect\n");
  } else {
    printf("FAILED to cancel iconnect\n");
  }

  ret = omx_irecvv(ep, segs, 2, 0, 0, NULL, &req);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to irecv (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  ret = omx_cancel(ep, &req, &result);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to cancel iconnect (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }
  if (result) {
    printf("successfully cancelled irecv\n");
  } else {
    printf("FAILED to cancel irecv\n");
  }

  ret = omx_iconnect(ep, nic_id, rid, 0x12345678, 0, NULL, &req);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to iconnect (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  ret = omx_cancel_notest(ep, &req, &result);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to cancel iconnect (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }
  if (result) {
    printf("successfully cancelled-notest iconnect\n");
  } else {
    printf("FAILED to cancel_notest iconnect\n");
  }

  ret = omx_irecvv(ep, segs, 2, 0, 0, NULL, &req);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to irecv (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  ret = omx_cancel_notest(ep, &req, &result);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to cancel iconnect (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }
  if (result) {
    printf("successfully cancelled-notest irecv\n");
  } else {
    printf("FAILED to cancel_notest irecv\n");
  }

  ret = omx_test_any(ep, 0, 0, &status, &result);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_CANCELLED);

  ret = omx_test_any(ep, 0, 0, &status, &result);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_CANCELLED);

  omx_close_endpoint(ep);
  omx_finalize();
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out_with_init:
  omx_finalize();
 out:
  return -1;
}
