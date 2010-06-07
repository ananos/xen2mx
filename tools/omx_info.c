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

#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>

#include "omx_lib.h"

static int verbose = 1;

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tonly display board id <n>\n");
  fprintf(stderr, " -q\tdo not display verbose messages\n");
  fprintf(stderr, " -v\tdisplay verbose messages\n");
}

static int
handle_one_board(unsigned index)
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  uint32_t board_index = index;
  struct omx_board_info board_info;
  omx_return_t ret;

  ret = omx__get_board_info(NULL, board_index, &board_info);
  if (ret == OMX_BOARD_NOT_FOUND)
    return 0;
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board #%d id, %s\n", index, omx_strerror(ret));
    return -1;
  }
  assert(index == board_index);

  omx__board_addr_sprintf(board_addr_str, board_info.addr);
  printf(" %s (board #%d name %s addr %s)\n",
	 board_info.hostname, index, board_info.ifacename, board_addr_str);

  if (verbose && board_info.drivername[0] != '\0')
    printf("   managed by driver '%s'\n", board_info.drivername);
  if (verbose && board_info.numa_node != (uint32_t) -1)
    printf("   attached to numa node %d\n", board_info.numa_node);
  if (board_info.status & OMX_BOARD_INFO_STATUS_DOWN)
    printf("   WARNING: interface is currently DOWN.\n");
  if (board_info.status & OMX_BOARD_INFO_STATUS_BAD_MTU)
    printf("   WARNING: MTU=%ld invalid\n", (unsigned long)board_info.mtu);
  if (verbose && board_info.status & OMX_BOARD_INFO_STATUS_HIGH_INTRCOAL)
    printf("   WARNING: high interrupt-coalescing\n");

  return 0;
}

int main(int argc, char *argv[])
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  omx_return_t ret;
  uint32_t bid = OMX_ANY_NIC;
  uint32_t max, emax, count;
  uint32_t status;
  uint64_t mapper_id;
  unsigned i;
  int c;

  while ((c = getopt(argc, argv, "b:qvh")) != -1)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'q':
      verbose = 0;
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

  if (verbose) {
    printf("Open-MX version " PACKAGE_VERSION "\n");
    printf(" build: " OMX_BUILD_STR "\n");
    printf("\n");
  }

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  /* get board and endpoint max */
  max = omx__driver_desc->board_max;
  emax = omx__driver_desc->endpoint_max;

  /* get board count */
  ret = omx__get_board_count(&count);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board count, %s\n", omx_strerror(ret));
    goto out;
  }
  printf("Found %ld boards (%ld max) supporting %ld endpoints each:\n",
	 (unsigned long) count, (unsigned long) max, (unsigned long) emax);

  /* print boards */
  if (bid == OMX_ANY_NIC)
    for(i=0; i<max; i++)
      handle_one_board(i);
  else
    handle_one_board(bid);

  /* get peer table state */
  ret = omx__driver_get_peer_table_state(&status, NULL, NULL, &mapper_id);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get peer table status, %s\n", omx_strerror(ret));
    goto out;
  }
  if (verbose) {
    /* print the common peer table */
    printf("\n");
    if (status & OMX_PEER_TABLE_STATUS_CONFIGURED) {
      omx__board_addr_sprintf(board_addr_str, mapper_id);
      printf("Peer table is ready, mapper is %s\n", board_addr_str);
    } else {
      printf("Peer table is not configured yet\n");
    }
    printf("================================================\n");
    omx__peers_dump("  %d) %s %s\n");
  }
  if (status & OMX_PEER_TABLE_STATUS_FULL) {
    printf("WARNING: peer table is full, some peers could not be added.\n");
  }

  return 0;

 out:
  return -1;
}
