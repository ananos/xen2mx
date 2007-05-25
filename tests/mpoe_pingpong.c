#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>

#include "mpoe_lib.h"

#define BID 0
#define EID 0
#define RID 0
#define ITER 100

static void
usage(void)
{
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -v\tverbose\n");
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -d <mac>\tset remote board mac address and switch to sender mode\n");
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
  fprintf(stderr, " -N <n>\tchange number of iterations [%d]\n", ITER);
}

struct param {
  uint32_t iter;
};

int main(int argc, char *argv[])
{
  int fd, ret;
  struct mpoe_cmd_open_endpoint open_param;
  void * recvq, * sendq, * eventq;
  char c;

  int bid = BID;
  int eid = EID;
  int rid = RID;
  int iter = ITER;
  uint8_t dest[6];
  int sender = 0;
  int verbose = 0;

  while ((c = getopt(argc, argv, "e:r:d:b:N:v")) != EOF)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'd':
      sscanf(optarg, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &dest[0], &dest[1], &dest[2], &dest[3], &dest[4], &dest[5]);
      sender = 1;
      break;
    case 'r':
      rid = atoi(optarg);
      break;
    case 'N':
      iter = atoi(optarg);
      break;
    case 'v':
      verbose = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
      usage();
      exit(-1);
      break;
    }

  fd = open(MPOE_DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  /* ok */
  open_param.board_index = bid;
  open_param.endpoint_index = eid;
  ret = ioctl(fd, MPOE_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach endpoint");
    goto out_with_fd;
  }

  /* mmap */
  sendq = mmap(0, MPOE_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_SENDQ_OFFSET);
  recvq = mmap(0, MPOE_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_RECVQ_OFFSET);
  eventq = mmap(0, MPOE_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_EVENTQ_OFFSET);

  if (sender) {
    /* sender */

    volatile union mpoe_evt * evt = eventq;
    struct mpoe_cmd_send_tiny tiny_param;
    struct param * param;
    int i;

    printf("Starting sender to %02x:%02x:%02x:%02x:%02x:%02x...\n",
	   dest[0], dest[1], dest[2], dest[3], dest[4], dest[5]);

    /* sending the param message */
    param = (void *) tiny_param.data;
    param->iter = iter;
    memcpy(tiny_param.hdr.dest_mac, dest, sizeof (tiny_param.hdr.dest_mac));
    tiny_param.hdr.dest_endpoint = rid;
    tiny_param.hdr.length = sizeof(struct param);
    tiny_param.hdr.match_info = 0x1234567887654321ULL;
    ret = ioctl(fd, MPOE_CMD_SEND_TINY, &tiny_param);
    if (ret < 0) {
      perror("ioctl/send/tiny");
      goto out_with_fd;
    }

    printf("Sent parameters (iter=%d)\n", param->iter);

    for(i=0; i<iter; i++) {
      if (verbose)
	printf("Iteration %d/%d\n", i, iter);

      /* wait for the message */
      while (evt->generic.type == MPOE_EVT_NONE) ;
      assert(evt->generic.type == MPOE_EVT_RECV_TINY);
      /* mark event as done */
      evt->generic.type = MPOE_EVT_NONE;
      /* next event */
      evt++;
      if ((void *) evt >= eventq + MPOE_EVENTQ_SIZE)
	evt = eventq;

      /* send a tiny message */
      memcpy(tiny_param.hdr.dest_mac, dest, sizeof (tiny_param.hdr.dest_mac));
      tiny_param.hdr.dest_endpoint = rid;
      tiny_param.hdr.length = 0;
      tiny_param.hdr.match_info = 0x1234567887654321ULL;
      ret = ioctl(fd, MPOE_CMD_SEND_TINY, &tiny_param);
      if (ret < 0) {
	perror("ioctl/send/tiny");
	goto out_with_fd;
      }
    }
    if (verbose)
      printf("Iteration %d/%d\n", i, iter);

  } else {
    /* receiver */

    volatile union mpoe_evt * evt = eventq;
    struct mpoe_cmd_send_tiny tiny_param;
    struct param * param;
    struct timeval tv1, tv2;
    unsigned long long us;
    int i;

    printf("Starting receiver...\n");

    printf("Waiting for parameters...\n");

    /* wait for the message */
    while (evt->generic.type == MPOE_EVT_NONE) ;
    assert(evt->generic.type == MPOE_EVT_RECV_TINY);
    /* mark event as done */
    evt->generic.type = MPOE_EVT_NONE;
    /* retrieve parameters */
    memcpy(dest, (void *) evt->tiny.src_mac, sizeof(evt->tiny.src_mac));
    rid = evt->tiny.src_endpoint;
    param = (void *) evt->tiny.data;
    iter = param->iter;
    /* next event */
    evt++;
    if ((void *) evt >= eventq + MPOE_EVENTQ_SIZE)
      evt = eventq;

    printf("Got parameters (iter=%d)\n", param->iter);

    gettimeofday(&tv1, NULL);

    for(i=0; i<iter; i++) {
      if (verbose)
	printf("Iteration %d/%d\n", i, iter);

      /* send a tiny message */
      memcpy(tiny_param.hdr.dest_mac, dest, sizeof (tiny_param.hdr.dest_mac));
      tiny_param.hdr.dest_endpoint = rid;
      tiny_param.hdr.length = 0;
      tiny_param.hdr.match_info = 0x1234567887654321ULL;
      ret = ioctl(fd, MPOE_CMD_SEND_TINY, &tiny_param);
      if (ret < 0) {
	perror("ioctl/send/tiny");
	goto out_with_fd;
      }

      /* wait for the message */
      while (evt->generic.type == MPOE_EVT_NONE) ;
      assert(evt->generic.type == MPOE_EVT_RECV_TINY);
      /* mark event as done */
      evt->generic.type = MPOE_EVT_NONE;
      /* next event */
      evt++;
      if ((void *) evt >= eventq + MPOE_EVENTQ_SIZE)
	evt = eventq;
    }
    if (verbose)
      printf("Iteration %d/%d\n", i, iter);

    gettimeofday(&tv2, NULL);
    us = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
    printf("Total Duration: %lld us\n", us);
    printf("Latency: %f us\n", ((float) us)/2./iter);
  }

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
