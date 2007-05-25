#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/time.h>

#include "mpoe_lib.h"

#define IFNAME "lo"
#define EP 3
#define ITER 10

static int
send_tiny(int fd, int i, struct mpoe_mac_addr * dest_addr)
{
  struct mpoe_cmd_send_tiny tiny_param;
  int ret;

  mpoe_mac_addr_copy(&tiny_param.hdr.dest_addr, dest_addr);
  tiny_param.hdr.dest_endpoint = EP;
  tiny_param.hdr.match_info = 0x1234567887654321ULL;

  sprintf(tiny_param.data, "message %d", i);
  tiny_param.hdr.length = strlen(tiny_param.data) + 1;

  ret = ioctl(fd, MPOE_CMD_SEND_TINY, &tiny_param);
  if (ret < 0) {
    perror("ioctl/send/tiny");
    return ret;
  }

  fprintf(stderr, "Successfully sent \"%s\"\n", (char*) tiny_param.data);
  return 0;
}

static int
send_medium(int fd, int i, struct mpoe_mac_addr * dest_addr, void * sendq)
{
  struct mpoe_cmd_send_medium_hdr medium_param;
  char * buffer = (char*)sendq + 23*4096;
  int ret;

  mpoe_mac_addr_copy(&medium_param.dest_addr, dest_addr);
  medium_param.dest_endpoint = EP;
  medium_param.match_info = 0x1234567887654321ULL;
  medium_param.sendq_page_offset = 23;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", i);
  medium_param.length = strlen(buffer) + 1;

  ret = ioctl(fd, MPOE_CMD_SEND_MEDIUM, &medium_param);
  if (ret < 0) {
    perror("ioctl/send/medium");
    return ret;
  }

  fprintf(stderr, "Successfully sent \"%s\"\n", buffer);
  return 0;
}

static int
wait_for_event(volatile union mpoe_evt ** evtp,
	       void * recvq, void * eventq)
{
  volatile union mpoe_evt * evt = *evtp;

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
    return -1;
  }

  /* mark event as done */
  evt->generic.type = MPOE_EVT_NONE;

  /* next event */
  evt++;
  if ((void *) evt >= eventq + MPOE_EVENTQ_SIZE)
    evt = eventq;
  *evtp = evt;

  return 0;
}

int main(void)
{
  int fd, ret;
  struct mpoe_cmd_open_endpoint open_param;
  struct mpoe_cmd_get_board_id board_id;
  volatile union mpoe_evt * evt;
  void * recvq, * sendq, * eventq;
  uint32_t count;
  int i;
  struct timeval tv1, tv2;
  char * ifname = IFNAME; /* FIXME: option to change it */
  struct mpoe_mac_addr dest_addr; /* FIXME: option to change it, or at least set to broadcast */
  char dest_addr_str[MPOE_MAC_ADDR_STRLEN];

  fd = open(MPOE_DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  /* buggy board index */
  open_param.board_index = 7;
  ret = ioctl(fd, MPOE_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach unknown interface");
  }

  /* get board count */
  ret = ioctl(fd, MPOE_CMD_GET_BOARD_COUNT, &count);
  if (ret < 0) {
    perror("get board id");
    goto out_with_fd;
  }

  /* find "lo" */
  for(i=0; i<count; i++) {
    board_id.board_index = i;
    ret = ioctl(fd, MPOE_CMD_GET_BOARD_ID, &board_id);
    if (ret < 0) {
      perror("get board id");
      goto out_with_fd;
    }
    if (!strcmp(board_id.board_name, ifname))
      goto found;
  }

  fprintf(stderr, "Cannot find interface '%s'\n", ifname);
  goto out_with_fd;

 found:
  mpoe_mac_addr_copy(&dest_addr, &board_id.board_addr);

  mpoe_mac_addr_sprintf(dest_addr_str, &dest_addr);
  fprintf(stderr, "Got board %s id #%d addr %s\n",
	  board_id.board_name, i, dest_addr_str);

  /* ok */
  open_param.board_index = i;
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

  evt = eventq;

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a tiny message */
    if (send_tiny(fd, i, &dest_addr) < 0)
      goto out_with_fd;
    if (wait_for_event(&evt, recvq, eventq) < 0)
      goto out_with_fd;
  }
  gettimeofday(&tv2, NULL);
  printf("tiny latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a medium message */
    if (send_medium(fd, i, &dest_addr, sendq) < 0)
      goto out_with_fd;
    if (wait_for_event(&evt, recvq, eventq) < 0)
      goto out_with_fd;
  }
  gettimeofday(&tv2, NULL);
  printf("medium latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
