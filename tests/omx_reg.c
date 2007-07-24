#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "omx__lib.h"
#include "omx__internals.h"

#define EP 3
#define ITER 10000
#define LENGTH (1024*1024*4*4)

static inline int
do_register(int fd, int id,
	   void * buffer1, unsigned long len1,
	   void * buffer2, unsigned long len2)
{
  struct omx_cmd_region_segment seg[2];
  struct omx_cmd_register_region reg;

  seg[0].vaddr = (uintptr_t) buffer1;
  seg[0].len = len1;
  seg[1].vaddr = (uintptr_t) buffer2;
  seg[1].len = len2;
  reg.nr_segments = 2;
  reg.id = id;
  reg.seqnum = 567; /* unused for now */
  reg.memory_context = 0ULL; /* unused for now */
  reg.segments = (uintptr_t) seg;

  return ioctl(fd, OMX_CMD_REGISTER_REGION, &reg);
}

static inline int
do_deregister(int fd, int id)
{
  struct omx_cmd_deregister_region dereg;
  dereg.id = id;
  return ioctl(fd, OMX_CMD_DEREGISTER_REGION, &dereg);
}

int main(void)
{
  int fd, ret;
  struct omx_cmd_open_endpoint open_param;
  int i;
  struct timeval tv1, tv2;
  char *buffer1, *buffer2;

  fd = open(OMX_DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  open_param.board_index = 0;
  open_param.endpoint_index = EP;
  ret = ioctl(fd, OMX_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach endpoint");
    goto out_with_fd;
  }
  fprintf(stderr, "Successfully attached endpoint %d/%d\n", 0, 34);

  buffer1 = malloc(LENGTH);
  buffer2 = malloc(LENGTH);
  if (!buffer1 || !buffer2) {
    fprintf(stderr, "Failed to allocate buffers\n");
    goto out_with_fd;
  }

  ret = do_register(fd, 34, buffer1, LENGTH, buffer2, LENGTH);
  if (ret < 0) {
    fprintf(stderr, "Failed to register (%m)\n");
    goto out_with_fd;
  }

  ret = do_register(fd, 34, buffer1, LENGTH, buffer2, LENGTH);
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

  for(i=0; i<ITER; i++) {

    ret = do_register(fd, 34, buffer1, LENGTH, buffer2, LENGTH);
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
  printf("%lld us\n", (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  free(buffer2);
  free(buffer1);

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
