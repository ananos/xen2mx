/*
 * Open-MX
 * Copyright © inria 2007-2011 (see AUTHORS file)
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
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "omx_lib.h"

#define EP 3
#define ITER 10000
#define LENGTH (1024*1024*4*4)

uint8_t cmdline_xen = 0;

static inline int
do_register(int fd, int id,
	   void * buffer1, unsigned long len1,
	   void * buffer2, unsigned long len2)
{
  struct omx_cmd_user_segment seg[2];
  struct omx_cmd_create_user_region reg;

  seg[0].vaddr = (uintptr_t) buffer1;
  seg[0].len = len1;
  seg[1].vaddr = (uintptr_t) buffer2;
  seg[1].len = len2;
  reg.nr_segments = 2;
  reg.id = id;
  reg.seqnum = 567; /* unused for now */
  reg.memory_context = 0ULL; /* unused for now */
  reg.segments = (uintptr_t) seg;

  if (cmdline_xen)
    return ioctl(fd, OMX_CMD_XEN_CREATE_USER_REGION, &reg);
  else
    return ioctl(fd, OMX_CMD_CREATE_USER_REGION, &reg);
}

static inline int
do_deregister(int fd, int id)
{
  struct omx_cmd_destroy_user_region dereg;
  dereg.id = id;
  if (cmdline_xen)
    return ioctl(fd, OMX_CMD_XEN_DESTROY_USER_REGION, &dereg);
  else
    return ioctl(fd, OMX_CMD_DESTROY_USER_REGION, &dereg);
}

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -l <n>\tchange buffer length [%d]\n", LENGTH);
  fprintf(stderr, " -N <n>\tchange the number of iterations [%d]\n", ITER);
}

int main(int argc, char *argv[])
{
  int fd, ret;
  struct omx_cmd_open_endpoint open_param;
  int i;
  struct timeval tv1, tv2;
  char *buffer1, *buffer2;
  int c;
  int length = LENGTH;
  int iter = ITER;

  while ((c = getopt(argc, argv, "l:N:hx")) != -1)
    switch (c) {
    case 'l':
      length = atoi(optarg);
      break;
    case 'x':
      cmdline_xen = 1;
      break;
    case 'N':
      iter = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  fd = open("/dev/" OMX_MAIN_DEVICE_NAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  open_param.board_index = 0;
  open_param.endpoint_index = EP;
  if (cmdline_xen)
    ret = ioctl(fd, OMX_CMD_XEN_OPEN_ENDPOINT, &open_param);
  else
    ret = ioctl(fd, OMX_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach endpoint");
    goto out_with_fd;
  }
  fprintf(stderr, "Successfully attached endpoint %d/%d\n", 0, EP);

  buffer1 = malloc(length);
  buffer2 = malloc(length);
  if (!buffer1 || !buffer2) {
    fprintf(stderr, "Failed to allocate buffers\n");
    goto out_with_fd;
  }

  ret = do_register(fd, 34, buffer1, length, buffer2, length);
  if (ret < 0) {
    fprintf(stderr, "Failed to register (%m)\n");
    goto out_with_fd;
  }

  ret = do_register(fd, 34, buffer1, length, buffer2, length);
  if (ret < 0) {
    fprintf(stderr, "Successfully couldn't register window again (%m)\n");
  }

  ret = do_deregister(fd, 35);
  if (ret < 0) {
    fprintf(stderr, "Successfully couldn't deregister unknown window (%m)\n");
  }

  ret = do_deregister(fd, 34);
  if (ret < 0) {
    fprintf(stderr, "Failed to deregister window (%m)\n");
    goto out_with_fd;
  }

  gettimeofday(&tv1, NULL);

  for(i=0; i<iter; i++) {

    ret = do_register(fd, 34, buffer1, length, buffer2, length);
    if (ret < 0) {
      fprintf(stderr, "Failed to register (%m)\n");
      goto out_with_fd;
    }

    ret = do_deregister(fd, 34);
    if (ret < 0) {
      fprintf(stderr, "Failed to deregister window (%m)\n");
      goto out_with_fd;
    }
  }

  gettimeofday(&tv2, NULL);
  printf("%d times register %d bytes => %lld us\n",
	 iter, length,
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  free(buffer2);
  free(buffer1);

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
