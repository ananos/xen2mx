/*
 * Open-MX
 * Copyright © inria 2007-2010 (see AUTHORS file)
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

#include <sys/ioctl.h>
#include <sys/time.h>
#include <getopt.h>

#include "omx_lib.h"

#define ITER 1000000

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
}

int
main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  omx_return_t ret;
  struct timeval tv1,tv2;
  struct omx_cmd_bench cmd;
  unsigned long long total, delay, olddelay;
  int i, err;
  int c;

  while ((c = getopt(argc, argv, "h")) != -1)
    switch (c) {
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  ret = omx_init();
  assert(ret == OMX_SUCCESS);

  ret = omx_open_endpoint(0, 0, 0, NULL, 0, &ep);
  assert(ret == OMX_SUCCESS);

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, NULL);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("plain IOCTL:      %lld ns   \t       (%lld us for %d iter)\n", delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_PARAMS;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ parameters:     +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_ALLOC;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send alloc:     +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_PREP;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send prepare:   +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_FILL;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send fill data: +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_DONE;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send done:      +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_ACQU;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ recv acquire:   +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_NOTIFY;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ recv notify:    +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_DONE;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
    assert(!err);
  }
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ recv done:      +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  return 0;
}
