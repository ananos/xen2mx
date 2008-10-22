/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>

#include "open-mx.h"

#define BID 0
#define EID OMX_ANY_ENDPOINT
#define ITER 10
#define PREDEFINED_LENGTHS -1
#define PARALLEL 4

static int verbose = 0;

static omx_return_t
one_iteration(omx_endpoint_t ep, omx_endpoint_addr_t addr,
	      char *buffer, char *buffer2, int length, int parallel, int seed)
{
  omx_request_t sreq[parallel], rreq[parallel], req;
  omx_status_t status;
  omx_return_t ret;
  uint32_t result;
  int i;

  /* initialize buffers to different values
   * so that it's easy to check bytes correctness
   * after the transfer
   */
  for(i=0; i<length; i++) {
    buffer[i] = (seed+i)%26+'a';
    buffer2[i] = (seed+i+13)%26+'a';
  }

  /* post N sends */
  for(i=0; i<parallel; i++) {
    ret = omx_isend(ep, buffer, length,
		    addr, 0x1234567887654321ULL,
		    NULL, &sreq[i]);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to send message length %d (%s)\n",
	      length, omx_strerror(ret));
      goto out;
    }
  }

  /* recv N with wait */
  for(i=0; i<parallel; i++) {
    ret = omx_irecv(ep, buffer2, length,
		    0, 0,
		    NULL, &rreq[i]);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to post a recv for a tiny message (%s)\n",
	      omx_strerror(ret));
      goto out;
    }

    ret = omx_wait(ep, &rreq[i], &status, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
  }

  /* wait for the first send to complete */
  ret = omx_wait(ep, &sreq[0], &status, &result, OMX_TIMEOUT_INFINITE);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  /* use peek to wait for the sends to complete */
  for(i=1; i<parallel; i++) {
    ret = omx_peek(ep, &req, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to peek (%s)\n",
              omx_strerror(ret));
      goto out;
    }
    if (req != sreq[i]) {
      fprintf(stderr, "Peek got request %p instead of %p\n",
              req, sreq[i]);
      ret = OMX_BAD_ERROR;
      goto out;
    }

    ret = omx_test(ep, &sreq[i], &status, &result);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
              omx_strerror(ret));
      goto out;
    }
  }

  /* check buffer contents */
  for(i=0; i<length; i++) {
    if (buffer[i] != buffer2[i]) {
      fprintf(stderr, "buffer invalid at offset %d, got '%c' instead of '%c'\n",
	      i, buffer2[i], buffer[i]);
      goto out;
    }
  }

  if (verbose)
    fprintf(stderr, "Successfully transferred %d bytes 4 times\n", length);

  return OMX_SUCCESS;

 out:
  return OMX_BAD_ERROR;
}

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -l <n>\tuse length instead of predefined ones\n");
  fprintf(stderr, " -P <n>\tsend multiple messages in parallel [%d]\n", PARALLEL);
  fprintf(stderr, " -s\tdo not disable shared communications\n");
  fprintf(stderr, " -S\tdo not disable self communications\n");
  fprintf(stderr, " -v\tenable verbose messages\n");
}

#define LENGTH1 13
#define LENGTH2 95
#define LENGTH3 13274
#define LENGTH4 9327485

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  uint64_t dest_board_addr;
  struct timeval tv1, tv2;
  int board_index = BID;
  int endpoint_index = EID;
  char hostname[OMX_HOSTNAMELEN_MAX];
  char ifacename[16];
  omx_endpoint_addr_t addr;
  int length = PREDEFINED_LENGTHS;
  int self = 0;
  int shared = 0;
  int parallel = PARALLEL;
  int c;
  int i;
  omx_return_t ret;
  char *buffer, *buffer2;

  while ((c = getopt(argc, argv, "e:b:l:P:sSvh")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'e':
      endpoint_index = atoi(optarg);
      break;
    case 'l':
      length = atoi(optarg);
      break;
    case 'P':
      parallel = atoi(optarg);
      break;
    case 's':
      shared = 1;
      break;
    case 'S':
      self = 1;
      break;
    case 'v':
      verbose = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  if (!self && !getenv("OMX_DISABLE_SELF"))
    putenv("OMX_DISABLE_SELF=1");

  if (!shared && !getenv("OMX_DISABLE_SHARED"))
    putenv("OMX_DISABLE_SHARED=1");

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_board_number_to_nic_id(board_index, &dest_board_addr);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find board %d nic id (%s)\n",
	    board_index, omx_strerror(ret));
    goto out;
  }

  ret = omx_open_endpoint(board_index, endpoint_index, 0x12345678, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_HOSTNAME, NULL, 0,
		     hostname, sizeof(hostname));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find board hostname (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_IFACENAME, NULL, 0,
		     ifacename, sizeof(ifacename));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find board iface name (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  printf("Using board #%d name '%s' hostname '%s'\n", board_index, ifacename, hostname);

  ret = omx_get_endpoint_addr(ep, &addr);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get local endpoint address (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  if (length == PREDEFINED_LENGTHS) {

    buffer = malloc(LENGTH4);
    if (!buffer)
      goto out_with_ep;
    buffer2 = malloc(LENGTH4);
    if (!buffer2) {
      free(buffer);
      goto out_with_ep;
    }

    gettimeofday(&tv1, NULL);
    for(i=0; i<ITER; i++) {
      /* send a tiny message */
      ret = one_iteration(ep, addr, buffer, buffer2, LENGTH1, parallel, i);
      if (ret != OMX_SUCCESS)
        goto out_with_ep;
    }
    gettimeofday(&tv2, NULL);
    printf("tiny (%d bytes) latency %lld us\n", 13,
	   (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

    gettimeofday(&tv1, NULL);
    for(i=0; i<ITER; i++) {
      /* send a small message */
      ret = one_iteration(ep, addr, buffer, buffer2, LENGTH2, parallel, i);
      if (ret != OMX_SUCCESS)
        goto out_with_ep;
    }
    gettimeofday(&tv2, NULL);
    printf("small (%d bytes) latency %lld us\n", 95,
	   (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

    gettimeofday(&tv1, NULL);
    for(i=0; i<ITER; i++) {
      /* send a medium message */
      ret = one_iteration(ep, addr, buffer, buffer2, LENGTH3, parallel, i);
      if (ret != OMX_SUCCESS)
        goto out_with_ep;
    }
    gettimeofday(&tv2, NULL);
    printf("medium (%d bytes) latency %lld us\n", 13274,
	   (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

    gettimeofday(&tv1, NULL);
    for(i=0; i<ITER; i++) {
      /* send a large message */
      ret = one_iteration(ep, addr, buffer, buffer2, LENGTH4, parallel, i);
      if (ret != OMX_SUCCESS)
        goto out_with_ep;
    }
    gettimeofday(&tv2, NULL);
    printf("large (%d bytes) latency %lld us\n", 9327485,
	   (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

    free(buffer2);
    free(buffer);

  } else {

    buffer = malloc(length);
    if (!buffer)
      goto out_with_ep;
    buffer2 = malloc(length);
    if (!buffer2) {
      free(buffer);
      goto out_with_ep;
    }

    gettimeofday(&tv1, NULL);
    for(i=0; i<ITER; i++) {
      /* send a large message */
      ret = one_iteration(ep, addr, buffer, buffer2, length, parallel, i);
      if (ret != OMX_SUCCESS)
        goto out_with_ep;
    }
    gettimeofday(&tv2, NULL);
    printf("message (%d bytes) latency %lld us\n", length,
	   (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

    free(buffer2);
    free(buffer);

  }

  omx_close_endpoint(ep);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
