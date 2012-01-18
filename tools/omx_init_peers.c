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
#include <stdlib.h>
#include <getopt.h>

#include "omx_lib.h"

#define OMX_PEERS_FILELINELEN_MAX (10 + 1 + OMX_HOSTNAMELEN_MAX + OMX_BOARD_ADDR_STRLEN + 1)

static int clear = 0;
static int verbose = 0;
static int done = 0;

static omx_return_t
omx__peer_add(uint64_t board_addr, char *hostname)
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  omx_return_t ret;

  omx__board_addr_sprintf(board_addr_str, board_addr);

  if (verbose)
    printf("Trying to adding peer %s address %s\n", hostname, board_addr_str);

  /* add the new peer */
  ret = omx__driver_peer_add(board_addr, hostname);
  if (ret != OMX_SUCCESS) {
    if (ret == OMX_BUSY) {
      fprintf(stderr, "Cannot add new peer, address (%s) already listed\n",
	      board_addr_str);
    } else {
      fprintf(stderr, "Failed to add new peer %s address %s (%s)\n",
	      hostname, board_addr_str, omx_strerror(ret));
    }
  }

  return ret;
}

static omx_return_t
omx__peers_read(const char * filename)
{
  char line[OMX_PEERS_FILELINELEN_MAX];
  FILE *file;
  omx_return_t ret;

  file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Cannot open file '%s'\n", filename);
    return OMX_BAD_ERROR;
  }

  while (fgets(line, OMX_PEERS_FILELINELEN_MAX, file)) {
    char hostname[OMX_HOSTNAMELEN_MAX];
    int addr_bytes[6];
    uint64_t board_addr;
    size_t len = strlen(line);

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
      ret = OMX_BAD_ERROR;
      goto out_with_file;
    }

    board_addr = ((((uint64_t) addr_bytes[0]) << 40)
		  + (((uint64_t) addr_bytes[1]) << 32)
		  + (((uint64_t) addr_bytes[2]) << 24)
		  + (((uint64_t) addr_bytes[3]) << 16)
		  + (((uint64_t) addr_bytes[4]) << 8)
		  + (((uint64_t) addr_bytes[5]) << 0));

    ret = omx__peer_add(board_addr, hostname);
    if (ret != OMX_SUCCESS && ret != OMX_BUSY)
      goto out_with_file;
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
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, "  => does not add any new peers\n");
  fprintf(stderr, "%s [options] filename\n", argv[0]);
  fprintf(stderr, "  => adds new peers from a file\n");
  fprintf(stderr, "%s [options] address hostname\n", argv[0]);
  fprintf(stderr, "  => adds a new single peer from the command line arguments\n");
  fprintf(stderr, "Options\n");
  fprintf(stderr, " -c\treplace existing peers with the new ones\n");
  fprintf(stderr, " -a\tappend new peers to existing ones (default)\n");
  fprintf(stderr, " -d\tmark the peer table configuration as done\n");
  fprintf(stderr, " -v\tverbose messages\n");
}

int
main(int argc, char *argv[])
{
  omx_return_t ret;
  int c;

  while ((c = getopt(argc, argv, "cadvh")) != -1)
    switch (c) {
    case 'c':
      clear = 1;
      break;
    case 'a':
      clear = 0;
      break;
    case 'd':
      done = 1;
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
    exit(-1);
  }

  if (clear) {
    printf("Clearing peers...\n");
    ret = omx__driver_peers_clear();
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to clear peers (%s)\n",
	      omx_strerror(ret));
      exit(-1);
    }
  }

  if (done) {
    printf("Marking the peer table configured as done...\n");
    ret = omx__driver_set_peer_table_state(1, 0, 0, -1);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to set peer table state (%s)\n",
	      omx_strerror(ret));
      exit(-1);
    }
  }

  if (argc > optind + 1) {
    /* two arguments given, take it as node + ip */
    char *board_addr_str = argv[optind];
    char *hostname = argv[optind+1];
    int addr_bytes[6];
    uint64_t board_addr;

    printf("Adding peer %s address %s\n", hostname, board_addr_str);

    /* parse the address */
    if (sscanf(board_addr_str, "%02x:%02x:%02x:%02x:%02x:%02x",
	       &addr_bytes[0], &addr_bytes[1], &addr_bytes[2],
	       &addr_bytes[3], &addr_bytes[4], &addr_bytes[5])
	!= 6) {
      fprintf(stderr, "Unrecognized address '%s'\n", board_addr_str);
      exit(-1);
    }

    board_addr = ((((uint64_t) addr_bytes[0]) << 40)
		  + (((uint64_t) addr_bytes[1]) << 32)
		  + (((uint64_t) addr_bytes[2]) << 24)
		  + (((uint64_t) addr_bytes[3]) << 16)
		  + (((uint64_t) addr_bytes[4]) << 8)
		  + (((uint64_t) addr_bytes[5]) << 0));

    ret = omx__peer_add(board_addr, hostname);

  } else if (argc == optind + 1) {
    /* single argument given, take it as a peer file */
    char *filename = argv[optind];
    printf("Adding peers from file %s...\n", filename);
    ret = omx__peers_read(filename);

  } else {
    printf("Not adding any peer\n");
  }

  return ret;
}
