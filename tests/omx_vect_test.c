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
#include <assert.h>

#include "open-mx.h"

#define BID 0
#define EID OMX_ANY_ENDPOINT
#define ITER 10
#define LEN 1048576

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
}

static void
vect_send_to_contig_recv(omx_endpoint_t ep, omx_endpoint_addr_t addr,
			 omx_seg_t *segs, int nseg,
			 char *sbuf, char *rbuf)
{
  omx_request_t sreq, rreq;
  omx_return_t ret;
  omx_status_t status;
  uint32_t result;
  uint32_t len;
  int i,j;

  memset(sbuf, 'a', LEN);
  for(i=0, len=0; i<nseg; i++) {
    memset(segs[i].ptr, 'b', segs[i].len);
    len += segs[i].len;
  }
  memset(rbuf, 'c', LEN);

  printf("sending %ld as %d segments\n", (unsigned long) len, (unsigned) nseg);

  ret = omx_irecv(ep, rbuf, len, 0, 0, NULL, &rreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_isendv(ep, segs, nseg, addr, 0, NULL, &sreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_wait(ep, &sreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_STATUS_SUCCESS);
  ret = omx_wait(ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_STATUS_SUCCESS);

  for(j=0; j<len; j++)
    if (rbuf[j] != 'b') {
      fprintf(stderr, "found %c instead of %c at %d\n", rbuf[j], 'b', j);
      assert(0);
    }
  memset(rbuf, 'c', len);
  printf("  rbuf touched as expected\n");

  for(j=0; j<LEN; j++)
    if (rbuf[j] != 'c') {
      fprintf(stderr, "found %c instead of %c at %d\n", rbuf[j], 'c', j);
      assert(0);
    }
  printf("  remaining rbuf not touched, as expected\n");
}

static void
contig_send_to_vect_recv(omx_endpoint_t ep, omx_endpoint_addr_t addr,
			 char *sbuf,
			 omx_seg_t *segs, int nseg, char *rbuf)
{
  omx_request_t sreq, rreq;
  omx_return_t ret;
  omx_status_t status;
  uint32_t result;
  uint32_t len;
  int i,j;

  for(i=0, len=0; i<nseg; i++) {
    len += segs[i].len;
  }

  memset(sbuf, 'a', LEN);
  memset(sbuf, 'b', len);
  memset(rbuf, 'c', LEN);

  printf("receiving %ld as %d segments\n", (unsigned long) len, (unsigned) nseg);

  ret = omx_irecvv(ep, segs, nseg, 0, 0, NULL, &rreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_isend(ep, sbuf, len, addr, 0, NULL, &sreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_wait(ep, &sreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_STATUS_SUCCESS);
  ret = omx_wait(ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_STATUS_SUCCESS);

  for(i=0; i<nseg; i++) {
    for(j=0; j<segs[i].len; j++) {
      char *buf = segs[i].ptr;
      if (buf[j] != 'b') {
	fprintf(stderr, "found %c instead of %c at %d(%d)\n", buf[j], 'b', i, j);
	assert(0);
      }
    }
    memset(segs[i].ptr, 'c', segs[i].len);
  }
  printf("  rbuf touched as expected\n");

  for(j=0; j<LEN; j++)
    if (rbuf[j] != 'c') {
      fprintf(stderr, "found %c instead of %c at %d\n", rbuf[j], 'c', j);
      assert(0);
    }
  printf("  remaining rbuf not touched, as expected\n");
}

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  uint64_t dest_board_addr;
  int board_index = BID;
  int endpoint_index = EID;
  char hostname[OMX_HOSTNAMELEN_MAX];
  char ifacename[16];
  omx_endpoint_addr_t addr;
  int c;
  int i;
  omx_seg_t seg[7];
  void * buffer1, * buffer2;
  omx_return_t ret;

  if (!getenv("OMX_DISABLE_SELF"))
    putenv("OMX_DISABLE_SELF=1");

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "e:b:h")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'e':
      endpoint_index = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
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

  buffer1 = malloc(LEN);
  buffer2 = malloc(LEN);
  if (buffer1 == NULL || buffer2 == NULL) {
    fprintf(stderr, "failed to allocate buffers\n");
    goto out_with_ep;
  }

  seg[0].ptr = buffer1 + 53;
  seg[0].len = 7; /* total  7, tiny */
  seg[1].ptr = buffer1 + 5672;
  seg[1].len = 23; /* total 30, tiny */
  seg[2].ptr = buffer1 + 8191;
  seg[2].len = 61; /* total 91, small */
  seg[3].ptr = buffer1 + 10001;
  seg[3].len = 26; /* total 117, small */
  seg[4].ptr = buffer1 + 11111;
  seg[4].len = 13456; /* total 13573, medium */
  seg[5].ptr = buffer1 + 50000;
  seg[5].len = 11111; /* total 24684, medium */
  seg[6].ptr = buffer1 + 100000;
  seg[6].len = 333333; /* total 357814, large */

  for(i=0; i<6; i++) { /* tiny/small/medium only so far */
    vect_send_to_contig_recv(ep, addr, seg, i+1, buffer1, buffer2);
  }

  for(i=0; i<4; i++) { /* tiny/small only so far */
    contig_send_to_vect_recv(ep, addr, buffer2, seg, i+1, buffer1);
  }

  omx_close_endpoint(ep);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
