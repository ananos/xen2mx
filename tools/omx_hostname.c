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
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <getopt.h>

#include "omx_lib.h"

#define BID 0

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options] hostname\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange board [%d] hostname\n", BID);
  fprintf(stderr, "%s [options] -c\n", argv[0]);
  fprintf(stderr, "\nclear all (non-local) peer names\n");
}

int main(int argc, char *argv[])
{
  uint8_t board_index = BID;
  omx_return_t ret;
  int clear;
  int c;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "b:ch")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'c':
      clear = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  if (argc < 2) {
    usage(argc, argv);
    exit(-1);
  }

  if (clear) {
    omx__driver_clear_peer_names();
  } else {
    omx__driver_set_hostname(board_index, argv[1]);
  }

  return 0;

 out:
  return -1;
}
