/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/time.h>

#include "../libopen-mx/omx_lib.h"

#define EP 3
#define ITER 10
#define LEN (64*1024)
#define SEND_BEGIN (LEN/8)
#define RECV_BEGIN (LEN/2+LEN/8)
#define COMM_LEN (LEN/4)
#define COOKIE 0xdeadbeef

static int
send_pull(int fd, uint32_t session_id, int id, int from, int to, int len)
{
  struct omx_cmd_send_pull pull_param;
  int ret;

  pull_param.dest_addr = -1; /* broadcast */
  pull_param.dest_endpoint = EP;
  pull_param.length = len;
  pull_param.session_id = session_id;
  pull_param.lib_cookie = COOKIE;
  pull_param.local_rdma_id = id;
  pull_param.local_offset = from;
  pull_param.remote_rdma_id = id;
  pull_param.remote_offset = to;

  ret = ioctl(fd, OMX_CMD_SEND_PULL, &pull_param);
  if (ret < 0) {
    perror("ioctl/send/pull");
    return ret;
  }

  fprintf(stderr, "Successfully sent pull request (cookie 0x%lx, length %ld)\n",
	  (unsigned long) COOKIE, (unsigned long) len);
  return 0;
}

static inline int
do_register(int fd, int id,
	    char * buffer, unsigned long len)
{
  struct omx_cmd_region_segment seg[2];
  struct omx_cmd_register_region reg;

  seg[0].vaddr = (uintptr_t) buffer;
  seg[0].len = len/2;
  seg[1].vaddr = (uintptr_t) buffer + len/2;
  seg[1].len = len/2;
  reg.nr_segments = 2;
  reg.id = id;
  reg.seqnum = 567; /* unused for now */
  reg.memory_context = 0ULL; /* unused for now */
  reg.segments = (uintptr_t) seg;

  return ioctl(fd, OMX_CMD_REGISTER_REGION, &reg);
}

int main(void)
{
  int fd, ret;
  struct omx_cmd_open_endpoint open_param;
  volatile union omx_evt * evt;
  void * recvq, * sendq, * eventq;
  char * buffer;
  uint32_t session_id;
  //  int i;
  //  struct timeval tv1, tv2;

  fd = open(OMX_DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    goto out;
  }

  /* ok */
  open_param.board_index = 0;
  open_param.endpoint_index = EP;
  ret = ioctl(fd, OMX_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach endpoint");
    goto out_with_fd;
  }
  fprintf(stderr, "Successfully attached endpoint %d/%d\n", 0, 34);

  ret = ioctl(fd, OMX_CMD_GET_ENDPOINT_SESSION_ID, &session_id);
  if (ret < 0) {
    perror("get session id");
    goto out_with_fd;
  }

  /* mmap */
  sendq = mmap(0, OMX_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_SENDQ_FILE_OFFSET);
  recvq = mmap(0, OMX_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_RECVQ_FILE_OFFSET);
  eventq = mmap(0, OMX_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_EVENTQ_FILE_OFFSET);
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

  {
    int i;
    for(i=0; i<LEN; i++)
      buffer[i] = 'a';
    for(i=0; i<COMM_LEN; i++)
      buffer[i+SEND_BEGIN] = 'b';
    for(i=0; i<COMM_LEN; i++)
      buffer[i+RECV_BEGIN] = 'c';
  }

  /* send a message */
  ret = send_pull(fd, session_id, 34, SEND_BEGIN, RECV_BEGIN, COMM_LEN);
  if (ret < 0)
    goto out_with_fd;

  evt = eventq;
  /* wait for the message */
  while (evt->generic.type == OMX_EVT_NONE) ;

  printf("received type %d\n", evt->generic.type);
  assert(evt->generic.type == OMX_EVT_PULL_DONE);
  assert(evt->pull_done.lib_cookie == COOKIE);
  printf("pull (cookie 0x%lx) transferred %ld bytes\n",
	 (unsigned long) evt->pull_done.lib_cookie,
	 (unsigned long) evt->pull_done.pulled_length);

  /* mark event as done */
  evt->generic.type = OMX_EVT_NONE;

  /* next event */
  evt++;
  if ((void *) evt >= eventq + OMX_EVENTQ_SIZE)
    evt = eventq;

  {
    int i;
    for(i=0; i<COMM_LEN; i++)
      if (buffer[i+SEND_BEGIN] != buffer[i+RECV_BEGIN]) {
	printf("buffer different at byte %d: '%c' instead of '%c'\n",
	       i, buffer[i+RECV_BEGIN], buffer[i+SEND_BEGIN]);
	break;
      }
    for(i=0; i<SEND_BEGIN; i++)
      if (buffer[i] != 'a') {
	printf("buffer different at byte %d: '%c' instead of 'a'\n",
	       i, buffer[i]);
	break;
      }
    for(i=SEND_BEGIN+COMM_LEN; i<RECV_BEGIN; i++)
      if (buffer[i] != 'a') {
	printf("buffer different at byte %d: '%c' instead of 'a'\n",
	       i, buffer[i]);
	break;
      }
    for(i=RECV_BEGIN+COMM_LEN; i<LEN; i++)
      if (buffer[i] != 'a') {
	printf("buffer different at byte %d: '%c' instead of 'a'\n",
	       i, buffer[i]);
	break;
      }
  }

  close(fd);

  return 0;

 out_with_fd:
  close(fd);
 out:
  return -1;
}
