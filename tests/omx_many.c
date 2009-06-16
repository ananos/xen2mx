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
#include <string.h>
#include <getopt.h>

#include "open-mx.h"

#define BID 0
#define RID 0
#define EID 0
#define ITER 1000

#define LEN1 13
#define LEN2 111
#define LEN3 1234
#define LEN4 12345
#define LEN5 123456
#define LEN6 1234567
#define NLEN 6

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -d <hostname>\tset remote peer name and switch to sender mode\n");
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
  fprintf(stderr, " -l <n>\tuse length instead of predefined ones\n");
  fprintf(stderr, " -N <n>\tchange number of iterations [%d]\n", ITER);
}

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  omx_return_t ret;
  int c;
  int i,j;

  int bid = BID;
  int eid = EID;
  int rid = RID;
  int iter = ITER;
  char my_hostname[OMX_HOSTNAMELEN_MAX];
  char my_ifacename[OMX_BOARD_ADDR_STRLEN];
  char *dest_hostname = NULL;
  uint64_t dest_addr;
  int sender = 0;
  char * buffer;

  int nlen = NLEN;
  int length[NLEN] = { LEN1, LEN2, LEN3, LEN4, LEN5, LEN6 };
  int maxlen = LEN6;

  while ((c = getopt(argc, argv, "b:e:d:r:l:N:h")) != -1)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'd':
      dest_hostname = strdup(optarg);
      sender = 1;
      break;
    case 'r':
      rid = atoi(optarg);
      break;
    case 'l':
      nlen = 1;
      length[0] = atoi(optarg);
      maxlen = length[0];
      break;
    case 'N':
      iter = atoi(optarg);
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

  if (dest_hostname) {
    ret = omx_hostname_to_nic_id(dest_hostname, &dest_addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Cannot find peer name %s\n", dest_hostname);
      goto out;
    }
  }

  ret = omx_open_endpoint(bid, eid, 0x12345678, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_HOSTNAME,
                     NULL, 0,
                     my_hostname, sizeof(my_hostname));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get endpoint hostname (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_IFACENAME,
                     NULL, 0,
                     my_ifacename, sizeof(my_ifacename));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get endpoint iface name (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  buffer = malloc(maxlen);
  if (!buffer) {
    fprintf(stderr, "Failed to allocate %d-bytes buffer\n", maxlen);
    goto out_with_ep;
  }

  printf("Successfully open endpoint %d for hostname '%s' iface '%s'\n",
         eid, my_hostname, my_ifacename);

  if (sender) {
    /* sender */

    omx_endpoint_addr_t addr;
    omx_request_t req;
    omx_status_t status;
    uint32_t result;

    printf("Starting sender to '%s' with length ", dest_hostname);
    for(i=0; i<nlen; i++)
      printf("%d ", length[i]);
    printf("...\n");

    ret = omx_connect(ep, dest_addr, rid, 0x12345678, OMX_TIMEOUT_INFINITE, &addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to connect (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }

    for(i=0; i<iter; i++) {
      for(j=0; j<nlen; j++) {
	ret = omx_isend(ep, buffer, length[j], addr, 0, NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to post isend, %s\n", omx_strerror(ret));
	  goto out_with_ep;
	}
      }
    }
    for(i=0; i<iter*nlen; i++) {
      omx_wait_any(ep, 0, 0, &status, &result, OMX_TIMEOUT_INFINITE);
      if (ret != OMX_SUCCESS || !result) {
	fprintf(stderr, "Failed to post wait any, %s\n", omx_strerror(ret));
	goto out_with_ep;
      }
    }

  } else {
    /* receiver */

    omx_request_t req;
    omx_status_t status;
    uint32_t result;

    printf("Starting receiver up to length %d ...\n", maxlen);

    for(i=0; i<iter*nlen; i++) {
      ret = omx_irecv(ep, buffer, maxlen, 0, 0, NULL, &req);
      if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Failed to post irecv, %s\n", omx_strerror(ret));
	goto out_with_ep;
      }
    }
    for(i=0; i<iter*nlen; i++) {
      omx_wait_any(ep, 0, 0, &status, &result, OMX_TIMEOUT_INFINITE);
      if (ret != OMX_SUCCESS || !result) {
	fprintf(stderr, "Failed to post wait any, %s\n", omx_strerror(ret));
	goto out_with_ep;
      }
    }
  }

  omx_close_endpoint(ep);
  free(dest_hostname);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  free(dest_hostname);
  return -1;
}
