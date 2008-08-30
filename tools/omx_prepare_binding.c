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
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include "omx_lib.h"

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options] [file]\n", argv[0]);
  fprintf(stderr, "  default output file is %s\n", OMX_PROCESS_BINDING_FILE);
  fprintf(stderr, "  -v\tverbose messages\n");
}

int main(int argc, char *argv[])
{
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  omx_return_t ret;
  char *file = OMX_PROCESS_BINDING_FILE;
  int outfd;
  FILE *output;
  uint32_t max, emax, count;
  int found, i, j;
  int verbose = 0;
  int c;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "vh")) != -1)
    switch (c) {
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

  if (optind < argc)
    file = argv[optind];

  outfd = open(file, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (outfd < 0) {
    fprintf(stderr, "Failed to open %s for writing, %m\n", file);
    goto out;
  }    
  output = fdopen(outfd, "w");
  assert(output);

  /* get board and endpoint max */
  max = omx__driver_desc->board_max;
  emax = omx__driver_desc->endpoint_max;

  /* get board count */
  ret = omx__get_board_count(&count);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board count, %s\n", omx_strerror(ret));
    goto out;
  }

  for(i=0, found=0; i<max && found<count; i++) {
    uint32_t board_index = i;
    struct omx_board_info board_info;

    ret = omx__get_board_info(NULL, board_index, &board_info);
    if (ret == OMX_BOARD_NOT_FOUND)
      continue;
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to read board #%d id, %s\n", i, omx_strerror(ret));
      goto out;
    }

    assert(i == board_index);
    found++;

    omx__board_addr_sprintf(board_addr_str, board_info.addr);

    for(j=0; j<emax; j++) {
      struct omx_cmd_get_endpoint_irq get_irq;
      char smp_affinity_path[10+strlen("/proc/irq/*/smp_affinity")];
      char line[OMX_PROCESS_BINDING_LENGTH_MAX], *end;
      int err;
      int fd;

      get_irq.board_index = board_index;
      get_irq.endpoint_index = j;
      err = ioctl(omx__globals.control_fd, OMX_CMD_GET_ENDPOINT_IRQ, &get_irq);
      if (err < 0) {
	if (verbose)
	  fprintf(stderr, "No IRQ found for endpoint %ld on board %ld (%s)\n",
		  (unsigned long) j, (unsigned long) i, board_addr_str);
        continue;
      }

      sprintf(smp_affinity_path, "/proc/irq/%d/smp_affinity", get_irq.irq);
      fd = open(smp_affinity_path, O_RDONLY);
      if (fd < 0) {
	if (errno == ENOENT) {
	  if (verbose)
	    fprintf(stderr, "No affinity found for IRQ %ld for endpoint %ld on board %ld (%s)\n",
		    (unsigned long) get_irq.irq, (unsigned long) j, (unsigned long) i, board_addr_str);
          continue;
	}
	fprintf(stderr, "Failed to open %s, %m\n", smp_affinity_path);
	goto out;
      }

      err = read(fd, line, OMX_PROCESS_BINDING_LENGTH_MAX);
      if (err < 0) {
	fprintf(stderr, "Failed to read %s, %m\n", smp_affinity_path);
	goto out;
      }
      line[err] = '\0';
      end = strchr(line, '\n');
      if (end)
	*end = '\0';

      fprintf(output, "board %s ep %ld irq %ld mask %s\n",
	      board_addr_str, (unsigned long) j, (unsigned long) get_irq.irq, line);
      if (verbose)
	printf("Found irq %ld mask %s for endpoint %ld on board %ld (%s)\n",
	       (unsigned long) get_irq.irq, line, (unsigned long) j, (unsigned long) i, board_addr_str);
    }
  }
  fclose(output);
  printf("Generated bindings in %s\n", file);

  return 0;

 out:
  return -1;
}
