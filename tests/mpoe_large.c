#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/time.h>

#include "mpoe_lib.h"
#include "mpoe_internals.h"

#define EP 3
#define ITER 10
#define LEN (1024*1024)

static int
send_pull(int fd, int id, int from, int to, int len)
{
  struct mpoe_cmd_send_pull pull_param;
  int ret;

  mpoe_mac_addr_set_bcast(&pull_param.dest_addr);
  pull_param.dest_endpoint = EP;
  pull_param.local_rdma_id = id;
  pull_param.local_offset = from;
  pull_param.remote_rdma_id = id;
  pull_param.remote_offset = to;
  pull_param.length = len;

  ret = ioctl(fd, MPOE_CMD_SEND_PULL, &pull_param);
  if (ret < 0) {
    perror("ioctl/send/pull");
    return ret;
  }

  fprintf(stderr, "Successfully sent pull request\n");
  return 0;
}

static inline int
do_register(int fd, int id,
	    void * buffer, unsigned long len)
{
  struct mpoe_cmd_region_segment seg;
  struct mpoe_cmd_register_region reg;

  seg.vaddr = (uintptr_t) buffer;
  seg.len = len;
  reg.nr_segments = 1;
  reg.id = id;
  reg.seqnum = 567; /* unused for now */
  reg.memory_context = 0ULL; /* unused for now */
  reg.segments = (uintptr_t) &seg;

  return ioctl(fd, MPOE_CMD_REGISTER_REGION, &reg);
}

int main(void)
{
  int fd, ret;
  struct mpoe_cmd_open_endpoint open_param;
  //  volatile union mpoe_evt * evt;
  void * recvq, * sendq, * eventq;
  void * buffer;
  //  int i;
  //  struct timeval tv1, tv2;

  fd = open(MPOE_DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  /* ok */
  open_param.board_index = 0;
  open_param.endpoint_index = EP;
  ret = ioctl(fd, MPOE_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach endpoint");
    goto out_with_fd;
  }
  fprintf(stderr, "Successfully attached endpoint %d/%d\n", 0, 34);

  /* mmap */
  sendq = mmap(0, MPOE_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_SENDQ_OFFSET);
  recvq = mmap(0, MPOE_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_RECVQ_OFFSET);
  eventq = mmap(0, MPOE_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_EVENTQ_OFFSET);
  printf("sendq at %p, recvq at %p, eventq at %p\n", sendq, recvq, eventq);

  /* allocate buffer */
  buffer = malloc(LEN);
  if (!buffer) {
    fprintf(stderr, "Failed to allocate buffer\n");
    goto out_with_fd;
  }

  /* create rdma window */
  ret = do_register(fd, 34, buffer, LEN);
  if (ret < 0) {
    fprintf(stderr, "Failed to register (%m)\n");
    goto out_with_fd;
  }

  /* send a message */
  ret = send_pull(fd, 34, LEN/8, LEN/2+LEN/8, LEN/4);
  if (ret < 0)
    goto out_with_fd;

  sleep(5);

#if 0
  evt = eventq;
  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {

    /* send a message */
    ret = send_tiny(fd, i);
    if (ret < 0)
      goto out_with_fd;

    /* wait for the message */
    while (evt->generic.type == MPOE_EVT_NONE) ;

    printf("received type %d\n", evt->generic.type);
    switch (evt->generic.type) {
    case MPOE_EVT_RECV_TINY:
      printf("tiny contains \"%s\"\n", evt->tiny.data);
      break;

    case MPOE_EVT_RECV_MEDIUM: {
      char * buf = recvq + ((char *) evt - (char *) eventq)/sizeof(*evt)*4096; /* FIXME: get pagesize somehow */
      printf("medium length %d contains \"%s\"\n", evt->medium.length, buf);
      break;
   }

    default:
      printf("unknown type\n");
      goto out_with_fd;
    }

    /* mark event as done */
    evt->generic.type = MPOE_EVT_NONE;

    /* next event */
    evt++;
    if ((void *) evt >= eventq + MPOE_EVENTQ_SIZE)
      evt = eventq;
  }
  gettimeofday(&tv2, NULL);
  printf("%lld us\n", (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));
#endif

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
