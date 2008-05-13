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
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>

#include "omx_lib.h"

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
}

int main(int argc, char *argv[])
{
  omx_return_t ret;
  uint32_t max, emax, count;
  int found, i;
  int c;

  printf("Open-MX version " PACKAGE_VERSION "\n");
  printf(" build: " OMX_BUILD_STR "\n");
  printf("\n");

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "h")) != -1)
    switch (c) {
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
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
  printf("Found %ld boards (%ld max) supporting %ld endpoints each\n",
	 (unsigned long) count, (unsigned long) max, (unsigned long) emax);

  for(i=0, found=0; i<max && found<count; i++) {
    uint8_t board_index = i;
    struct omx_board_info board_info;
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];

    ret = omx__get_board_info(NULL, board_index, &board_info);
    if (ret == OMX_BOARD_NOT_FOUND)
      continue;
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to read board #%d id, %s\n", i, omx_strerror(ret));
      goto out;
    }

    assert(i == board_index);
    found++;

    printf("\n");
    omx__board_addr_sprintf(board_addr_str, board_info.addr);

    printf("%s (board #%d name %s addr %s)\n",
	   board_info.hostname, i, board_info.ifacename, board_addr_str);

    if (board_info.numa_node != -1)
      printf("  attached to numa node %d\n", board_info.numa_node);
    printf("==============================================\n");

    omx__peers_dump("  %d) %s %s\n");
  }

  return 0;

 out:
  return -1;
}
