#include <sys/ioctl.h>
#include <sys/time.h>

#include "omx_lib.h"
#include "omx_io.h"

#define ITER 100000

int
main(void)
{
  omx_endpoint_t ep;
  omx_return_t ret;
  struct timeval tv1,tv2;
  struct omx_cmd_bench cmd;
  unsigned long long delay;
  int i, err;

  ret = omx_init();
  assert(ret == OMX_SUCCESS);

  ret = omx_open_endpoint(0, 0, 0, &ep);
  assert(ret == OMX_SUCCESS);

  cmd.hdr.type = OMX_CMD_BENCH_TYPE_RECV_DONE;

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++)
    err = ioctl(ep->fd, OMX_CMD_BENCH, &cmd);
  gettimeofday(&tv2, NULL);

  delay = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
  printf("%lld us for %d iter => %lld ns per iter\n", delay, ITER, delay*1000ULL/ITER);

  return 0;
}
