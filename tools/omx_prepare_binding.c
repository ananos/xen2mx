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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include "omx_lib.h"

static int verbose = 0;
static uint32_t emax;

#define OMX_PROC_INTERRUPTS_LENGTH_MAX 256
#define OMX_IFACE_SLICE_MAX 128

static int
omx__try_prepare_board(FILE *output, uint32_t board_index)
{
  int slice_irq[OMX_IFACE_SLICE_MAX] = { [0 ... OMX_IFACE_SLICE_MAX-1] = 0 };
  char line[OMX_PROC_INTERRUPTS_LENGTH_MAX];
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  struct omx_board_info board_info;
  omx_return_t ret;
  FILE *file;
  int slicemax = 0, slicemodulo;
  int j;

  ret = omx__get_board_info(NULL, board_index, &board_info);
  if (ret == OMX_BOARD_NOT_FOUND)
    return 0;
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board #%ld id, %s\n",
	    (unsigned long) board_index, omx_strerror(ret));
    return -1;
  }
  omx__board_addr_sprintf(board_addr_str, board_info.addr);

  if (verbose)
    fprintf(stderr, "Looking at board %ld (%s)\n",
	    (unsigned long) board_index, board_addr_str);

  file = fopen("/proc/interrupts", "r");
  if (!file) {
    fprintf(stderr, "Cannot read /proc/interrupts\n");
    return -1;
  }

  if (verbose)
    fprintf(stderr, "  Trying to find out interface %s interrupts\n",
	    board_info.ifacename);

  while (fgets(line, OMX_PROC_INTERRUPTS_LENGTH_MAX, file)) {
    char *end, *tmp, *slicename;

    end = strchr(line, '\n');
    if (!end) {
      fprintf(stderr, "/proc/interrupts line are too long, OMX_PROC_INTERRUPTS_LENGTH_MAX (%d) should be increased\n",
	      OMX_PROC_INTERRUPTS_LENGTH_MAX);
      fclose(file);
      return -1;
    }
    *end = '\0';

    if (!strchr(line, ':'))
      /* no colon, this line is useless */
      continue;

    slicename = strrchr(line, ' ');
    if (!slicename)
      /* no separator, this line is useless */
      continue;
    slicename++;

    tmp = strstr(slicename, board_info.ifacename);
    if (tmp) {
      int irq,slice,index;

      irq = atoi(line);

      /* ignore Tx interrupts */
      if (strcasestr(slicename, "tx")) {
	/* FIXME: add an option to cancel ignoring */
	if (verbose)
	  fprintf(stderr, "    Ignoring Tx interrupt %d name %s\n", irq, slicename);
	continue;
      }

      /* hide the ifacename within the slicename while searching for the slice number */
      memset(tmp, 'X', strlen(board_info.ifacename));

      index = strcspn(slicename, "0123456789");
      tmp = slicename + index;
      if (*tmp == '\0') {
	if (verbose)
	  fprintf(stderr, "    Found no slice number for irq %d in slice %s\n",
		  irq, slicename);
	/* FIXME: assume it's slice 0, and abort if already known */
        continue;
      }
      slice = atoi(tmp);

      if (verbose)
	fprintf(stderr, "    Found irq %d for iface %s slice %d\n", irq, board_info.ifacename, slice);

      if (slice < 0 || slice >= OMX_IFACE_SLICE_MAX) {
	abort();
      }
	
      slice_irq[slice] = irq;
      if (slice >= slicemax)
	slicemax = slice+1;
    }
  }

  fclose(file);

  if (verbose)
    fprintf(stderr, "  Trying to associate interface %s interrupts with endpoints\n",
	    board_info.ifacename);

  /* if we have a contigous set of interrupts, if so, use it as a modulo key */
  slicemodulo = slicemax;
  for(j=0; j<slicemax; j++) {
    if (!slice_irq[j]) {
      if (verbose)
	fprintf(stderr, "    Non-contigous slice range found (max=%d while %d missing), disabling modulo\n",
		slicemax, slicemodulo);
      slicemodulo = 0;
    }
  }

  for(j=0; j<emax; j++) {
    char smp_affinity_path[10+strlen("/proc/irq/*/smp_affinity")];
    char line[OMX_PROCESS_BINDING_LENGTH_MAX], *end;
    int slice, irq;
    int err;
    int fd;

    slice = slicemodulo ? j%slicemodulo : j;
    irq = slice_irq[slice];
    if (!irq) {
      if (verbose)
	fprintf(stderr, "    Found no irq for endpoint %d\n", j);
      continue;
    }

    sprintf(smp_affinity_path, "/proc/irq/%d/smp_affinity", irq);
    fd = open(smp_affinity_path, O_RDONLY);
    if (fd < 0) {
      if (errno == ENOENT) {
	if (verbose)
	  fprintf(stderr, "    No affinity found for IRQ %ld for endpoint %ld on board %ld (%s)\n",
		  (unsigned long) irq, (unsigned long) j, (unsigned long) board_index, board_addr_str);
        continue;
      }
      fprintf(stderr, "Failed to open %s, %m\n", smp_affinity_path);
      return -1;
    }

    err = read(fd, line, OMX_PROCESS_BINDING_LENGTH_MAX);
    if (err < 0) {
      fprintf(stderr, "Failed to read %s, %m\n", smp_affinity_path);
      return -1;
    }
    line[err] = '\0';
    end = strchr(line, '\n');
    if (end)
      *end = '\0';

    fprintf(output, "board %s ep %ld irq %ld mask %s\n",
	    board_addr_str, (unsigned long) j, (unsigned long) irq, line);
    if (verbose)
      printf("    Found irq %ld mask %s for endpoint %ld on board %ld (%s)\n",
	     (unsigned long) irq, line, (unsigned long) j, (unsigned long) board_index, board_addr_str);
  }

  return 1;
}

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options] [file]\n", argv[0]);
  fprintf(stderr, "  default output file is %s\n", OMX_PROCESS_BINDING_FILE);
  fprintf(stderr, "  -v\tverbose messages\n");
}

int main(int argc, char *argv[])
{
  omx_return_t ret;
  char *file = OMX_PROCESS_BINDING_FILE;
  int outfd;
  FILE *output;
  uint32_t max, count;
  int found, i;
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
    int err = omx__try_prepare_board(output, i);
    if (err < 0)
      goto out;
    if (err)
      found++;
  }

  fclose(output);
  printf("Generated bindings in %s\n", file);

  return 0;

 out:
  return -1;
}
