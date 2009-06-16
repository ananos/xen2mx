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
#include <getopt.h>
#include <assert.h>

#include "open-mx.h"

#define BID 0
#define EID OMX_ANY_ENDPOINT

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -s\tdo not disable shared communications\n");
  fprintf(stderr, " -S\tdo not disable self communications\n");
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
  int self = 0;
  int shared = 0;
  int c;
  omx_return_t ret;
  omx_status_t status;
  uint32_t result;

  while ((c = getopt(argc, argv, "e:b:sSh")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'e':
      endpoint_index = atoi(optarg);
      break;
    case 's':
      shared = 1;
      break;
    case 'S':
      self = 1;
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

  ret = omx_isend(ep, NULL, 0, addr, 0x1, NULL, NULL);
  assert(ret == OMX_SUCCESS);
  printf("posted send\n");

  ret = omx_probe(ep, 0x1, -1ULL, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.match_info == 0x01);
  printf("probe found exact match\n");

  ret = omx_iprobe(ep, 0x2, -1ULL, &status, &result);
  assert(ret == OMX_SUCCESS);
  assert(!result);
  printf("iprobe did not found match with wrong bits\n");

  ret = omx_iprobe(ep, 0, -2ULL, &status, &result);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.match_info == 0x01);
  printf("iprobe found match with mask\n");

  ret = omx_irecv(ep, NULL, 0, 0, -2ULL, NULL, NULL);
  assert(ret == OMX_SUCCESS);
  printf("posted recv with mask\n");

  ret = omx_iprobe(ep, 0, -2ULL, &status, &result);
  assert(ret == OMX_SUCCESS);
  assert(!result);
  printf("iprobe cannot found match with mask anymore\n");

  omx_close_endpoint(ep);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
