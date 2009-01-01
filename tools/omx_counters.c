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
#include <stdlib.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <errno.h>

#include "omx_lib.h"

#define BID 0

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange board id [%d]\n", BID);
  fprintf(stderr, " -s\treport shared communication counters\n");
  fprintf(stderr, " -c\tclear counters\n");
  fprintf(stderr, " -q\tonly display non-null counters [default]\n");
  fprintf(stderr, " -v\talso display null counters\n");
}

int main(int argc, char *argv[])
{
  struct omx_board_info board_info;
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  uint32_t board_index = BID;
  omx_return_t ret;
  uint32_t counters[OMX_COUNTER_INDEX_MAX];
  int clear = 0;
  int verbose = 0;
  struct omx_cmd_get_counters get_counters;
  int i, err;
  int c;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "b:scqvh")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 's':
      board_index = OMX_SHARED_FAKE_IFACE_INDEX;
      break;
    case 'c':
      clear = 1;
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

  /* get the board id */
  ret = omx__get_board_info(NULL, board_index, &board_info);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board #%d id, %s\n", board_index, omx_strerror(ret));
    goto out;
  }
  omx__board_addr_sprintf(board_addr_str, board_info.addr);
  get_counters.board_index = board_index;
  get_counters.clear = clear;
  get_counters.buffer_addr = (uintptr_t) counters;
  get_counters.buffer_length = sizeof(counters);
  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_COUNTERS, &get_counters);
  if (err < 0) {
    if (clear && errno == EPERM)
      perror("Clearing counters");
    goto out;
  }
  OMX_VALGRIND_MEMORY_MAKE_READABLE(counters, sizeof(counters));

  if (board_index == OMX_SHARED_FAKE_IFACE_INDEX)
    printf("%s (addr %s)\n",
	   board_info.hostname, board_addr_str);
  else
    printf("%s (board #%u name %s addr %s)\n",
	   board_info.hostname, board_index, board_info.ifacename, board_addr_str);
  printf("=======================================================\n");

  for(i=0; i<OMX_COUNTER_INDEX_MAX; i++)
    if (counters[i] || verbose)
      printf("%03d: % 9ld %s\n", i, (unsigned long) counters[i], omx_strcounter(i));

  return 0;

 out:
  return -1;
}
