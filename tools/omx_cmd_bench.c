#include <sys/ioctl.h>
#include <sys/time.h>

#include "omx_lib.h"
#include "omx_io.h"

#define ITER 1000000

int
main(void)
{
  omx_endpoint_t ep;
  omx_return_t ret;
  struct timeval tv1,tv2;
  struct omx_cmd_bench cmd;
  unsigned long long total, delay, olddelay;
  int i, err;

  ret = omx_init();
  assert(ret == OMX_SUCCESS);

  ret = omx_open_endpoint(0, 0, 0, &ep);
  assert(ret == OMX_SUCCESS);

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, NULL);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("plain IOCTL:      %lld ns   \t       (%lld us for %d iter)\n", delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_PARAMS;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ parameters:     +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_ALLOC;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send alloc:     +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_PREP;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send prepare:   +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_FILL;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send fill data: +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_SEND_DONE;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ send done:      +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_ACQU;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ recv acquire:   +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_ALLOC;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ recv alloc:     +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_DONE;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);
  total = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  delay = total*1000ULL/ITER;
  printf("+ recv done:      +%lld ns =>\t%lld ns (%lld us for %d iter)\n", delay-olddelay, delay, total, ITER);
  olddelay = delay;

  return 0;
}
