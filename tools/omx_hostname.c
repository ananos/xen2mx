/*
 * Open-MX
 * Copyright © inria 2007-2009 (see AUTHORS file)
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
#include <stdlib.h>
#include <sys/ioctl.h>
#include <getopt.h>

#include "omx_lib.h"

#define BID 0

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\t\toperate on board [%d]\n", BID);
  fprintf(stderr, " -n <hostname>\tset the board hostname\n");
  fprintf(stderr, " -c\t\tclear all (non-local) peer names\n");
  fprintf(stderr, " -v\t\tverbose messages\n");
}

int main(int argc, char *argv[])
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  struct omx_board_info board_info;
  uint8_t board_index = BID;
  omx_return_t ret;
  char *hostname = NULL;
  int verbose = 0;
  int clear = 0;
  int c;

  while ((c = getopt(argc, argv, "b:n:cvh")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'n':
      hostname = optarg;
      break;
    case 'c':
      clear = 1;
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

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  ret = omx__get_board_info(NULL, board_index, &board_info);
  if (ret == OMX_BOARD_NOT_FOUND) {
    fprintf(stderr, "Cannot find board #%d\n", board_index);
    goto out;
  }
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board #%d id, %s\n", board_index, omx_strerror(ret));
    goto out;
  }
  omx__board_addr_sprintf(board_addr_str, board_info.addr);

  /* if verbose or doing nothing, print current name */
  if (verbose || (!clear && !hostname))
    printf("Board #%d (%s %s) hostname was %s\n",
	   board_index, board_info.ifacename, board_addr_str, board_info.hostname);

  if (hostname) {
    if (verbose)
      printf("Changing board #%d (%s %s) hostname into %s\n",
           board_index, board_info.ifacename, board_addr_str, hostname);
    ret = omx__driver_set_hostname(board_index, hostname);
    if (ret != OMX_SUCCESS)
      fprintf(stderr, "Failed to change hostname, %s\n", omx_strerror(ret));
  }

  if (clear) {
    if (verbose)
      printf("Clearing peer names\n");
    ret = omx__driver_clear_peer_names();
    if (ret != OMX_SUCCESS)
      fprintf(stderr, "Failed to clear peer names, %s\n", omx_strerror(ret));
  }

  return 0;

 out:
  return -1;
}
