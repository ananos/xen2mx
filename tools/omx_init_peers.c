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
#include <getopt.h>

#include "omx_lib.h"

#define OMX_PEERS_FILELINELEN_MAX (10 + 1 + OMX_HOSTNAMELEN_MAX + OMX_BOARD_ADDR_STRLEN + 1)

omx_return_t
omx__peers_read(const char * filename)
{
  char line[OMX_PEERS_FILELINELEN_MAX];
  FILE *file;
  omx_return_t ret;

  file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Cannot open file '%s'\n", filename);
    return OMX_INVALID_PARAMETER;
  }

  while (fgets(line, OMX_PEERS_FILELINELEN_MAX, file)) {
    char hostname[OMX_HOSTNAMELEN_MAX];
    int addr_bytes[6];
    uint64_t board_addr;
    size_t len = strlen(line);
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];

    /* ignore comments and empty lines */
    if (line[0] == '#' || len == 1)
      continue;

    /* clean the line \n */
    if (line[len-1] == '\n')
      line[len-1] = '\0';

    /* parse a line */
    if (sscanf(line, "%02x:%02x:%02x:%02x:%02x:%02x %s",
	       &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
	       &addr_bytes[3], &addr_bytes[4], &addr_bytes[5],
	       hostname)
	!= 7) {
      fprintf(stderr, "Unrecognized peer line '%s'\n", line);
      ret = OMX_INVALID_PARAMETER;
      goto out_with_file;
    }

    board_addr = ((((uint64_t) addr_bytes[0]) << 40)
		  + (((uint64_t) addr_bytes[1]) << 32)
		  + (((uint64_t) addr_bytes[2]) << 24)
		  + (((uint64_t) addr_bytes[3]) << 16)
		  + (((uint64_t) addr_bytes[4]) << 8)
		  + (((uint64_t) addr_bytes[5]) << 0));
    omx__board_addr_sprintf(board_addr_str, board_addr);

    /* add the new peer */
    ret = omx__driver_peer_add(board_addr, hostname);
    if (ret != OMX_SUCCESS) {
      if (ret == OMX_BUSY) {
	fprintf(stderr, "Cannot add new peer, address (%s) already listed\n",
		board_addr_str);
	/* continue */
      } else {
	fprintf(stderr, "Failed to add new peer '%s' (%s)\n",
		line, omx_strerror(ret));
	goto out_with_file;
      }
    }
  }

  fclose(file);

  return OMX_SUCCESS;

 out_with_file:
  fclose(file);
  return ret;
}

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options] filename\n", argv[0]);
  fprintf(stderr, " -c <n>\treplace existing peers with the new ones (default)\n");
  fprintf(stderr, " -a <n>\tappend new peers to existing ones\n");
}

static int clear = 1;

int
main(int argc, char *argv[])
{
  omx_return_t ret;
  char c;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    exit(-1);
  }

  while ((c = getopt(argc, argv, "cah")) != EOF)
    switch (c) {
    case 'c':
      clear = 1;
      break;
    case 'a':
      clear = 0;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  if (optind >= argc) {
    fprintf(stderr, "Missing peers filename\n");
    exit(-1);
  }

  if (clear) {
    ret = omx__driver_peers_clear();
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to clear peers (%s)\n",
	      omx_strerror(ret));
      exit(-1);
    }
  }

  ret = omx__peers_read(argv[optind]);

  return ret;
}
