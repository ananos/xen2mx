/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
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

static omx_return_t
one_iteration(omx_endpoint_t ep, omx_endpoint_addr_t addr,
	      int length, int seed)
{
  omx_request_t sreq[4], rreq[4], req;
  omx_status_t status;
  char *buffer, *buffer2;
  omx_return_t ret;
  uint32_t result;
  int i;

  buffer = malloc(length);
  if (!buffer)
    return OMX_NO_RESOURCES;
  buffer2 = malloc(length);
  if (!buffer2) {
    free(buffer);
    return OMX_NO_RESOURCES;
  }

  /* initialize buffers to different values
   * so that it's easy to check bytes correctness
   * after the transfer
   */
  for(i=0; i<length; i++) {
    buffer[i] = (seed+i)%26+'a';
    buffer2[i] = (seed+i+13)%26+'a';
  }

  /* post 4 sends */
  for(i=0; i<4; i++) {
    ret = omx_isend(ep, buffer, length,
		    0x1234567887654321ULL, addr,
		    NULL, &sreq[i]);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to send message length %d (%s)\n",
	      length, omx_strerror(ret));
      goto out;
    }
  }

  /* recv 4 with wait */
  for(i=0; i<4; i++) {
    ret = omx_irecv(ep, buffer2, length,
		    0, 0,
		    NULL, &rreq[i]);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to post a recv for a tiny message (%s)\n",
	      omx_strerror(ret));
      goto out;
    }

    ret = omx_wait(ep, &rreq[i], &status, &result);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
	      omx_strerror(ret));
      goto out;
    }
  }

  /* wait for the first send to complete */
  ret = omx_wait(ep, &sreq[0], &status, &result);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  /* use peek to wait for the sends to complete */
  for(i=1; i<4; i++) {
    ret = omx_peek(ep, &req, &result);
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
	      i, buffer[i], buffer2[i]);
      goto out;
    }
  }
  fprintf(stderr, "Successfully transferred %d bytes 4 times\n", length);

  free(buffer2);
  free(buffer);
  return OMX_SUCCESS;

 out:
  free(buffer2);
  free(buffer);
  return OMX_BAD_ERROR;
}

static void
usage(void)
{
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
}

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
  char c;
  int i;
  omx_return_t ret;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "e:b:h")) != EOF)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'e':
      endpoint_index = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
      usage();
      exit(-1);
      break;
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

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a tiny message */
    ret = one_iteration(ep, addr, 13, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("tiny latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a small message */
    ret = one_iteration(ep, addr, 95, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("small latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a medium message */
    ret = one_iteration(ep, addr, 13274, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("medium latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a large message */
    ret = one_iteration(ep, addr, 1327485, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("large latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
