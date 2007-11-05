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
#include <malloc.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#include "../libopen-mx/omx_lib.h"

#define EP 3
#define ITER 10
#define BUFFER_ALIGN (2*4096) /* page-aligned on any arch */
#define PULL_LENGTH (16*4096)
#define BUFFER_LENGTH (19*4096) /* PULL_LENGTH + BUFFER_ALIGN + 4096 */
#define SEND_OFFSET 23
#define RECV_OFFSET 57
#define COOKIE 0xdeadbeef
#define SEND_RDMA_ID 34
#define RECV_RDMA_ID 35

static int
send_pull(int fd, uint32_t session_id)
{
  struct omx_cmd_send_pull pull_param;
  int ret;

  pull_param.peer_index = 0; /* myself? */
  pull_param.dest_endpoint = EP;
  pull_param.length = PULL_LENGTH;
  pull_param.session_id = session_id;
  pull_param.lib_cookie = COOKIE;
  pull_param.local_rdma_id = RECV_RDMA_ID;
  pull_param.local_offset = RECV_OFFSET;
  pull_param.remote_rdma_id = SEND_RDMA_ID;
  pull_param.remote_offset = SEND_OFFSET;

  ret = ioctl(fd, OMX_CMD_SEND_PULL, &pull_param);
  if (ret < 0) {
    perror("ioctl/send/pull");
    return ret;
  }

  fprintf(stderr, "Successfully sent pull request (cookie 0x%lx, length %ld)\n",
	  (unsigned long) COOKIE, (unsigned long) PULL_LENGTH);
  return 0;
}

static inline int
do_register(int fd, int id, char * buffer, int len)
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
  struct omx_endpoint_desc *desc;
  void * recvq, * sendq, * exp_eventq;
  char * send_buffer, * recv_buffer;
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

  /* mmap */
  desc = mmap(0, OMX_ENDPOINT_DESC_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_ENDPOINT_DESC_FILE_OFFSET);
  sendq = mmap(0, OMX_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_SENDQ_FILE_OFFSET);
  recvq = mmap(0, OMX_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_RECVQ_FILE_OFFSET);
  exp_eventq = mmap(0, OMX_EXP_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_EXP_EVENTQ_FILE_OFFSET);
  printf("sendq at %p, recvq at %p, exp eventq at %p\n", sendq, recvq, exp_eventq);

  /* allocate buffers */
  send_buffer = memalign(BUFFER_ALIGN, BUFFER_LENGTH);
  if (!send_buffer) {
    fprintf(stderr, "Failed to allocate send buffer\n");
    goto out_with_fd;
  }
  recv_buffer = memalign(BUFFER_ALIGN, BUFFER_LENGTH);
  if (!recv_buffer) {
    fprintf(stderr, "Failed to allocate recv buffer\n");
    goto out_with_send_buffer;
  }

  /* create rdma windows */
  ret = do_register(fd, SEND_RDMA_ID, send_buffer, BUFFER_LENGTH);
  if (ret < 0) {
    fprintf(stderr, "Failed to register send buffer (%m)\n");
    goto out_with_recv_buffer;
  }
  ret = do_register(fd, RECV_RDMA_ID, recv_buffer, BUFFER_LENGTH);
  if (ret < 0) {
    fprintf(stderr, "Failed to register recv buffer (%m)\n");
    goto out_with_send_register;
  }

  {
    int i;
    for(i=0; i<BUFFER_LENGTH; i++) {
      send_buffer[i] = 'a';
      recv_buffer[i] = 'b';
    }
    for(i=0; i<PULL_LENGTH; i++) {
      send_buffer[i+SEND_OFFSET] = 'k' + (i%10);
      recv_buffer[i+RECV_OFFSET] = 'c';
    }
  }

  /* send a message */
  ret = send_pull(fd, desc->session_id);
  if (ret < 0)
    goto out_with_recv_register;

  evt = exp_eventq;
  /* wait for the message */
  while (evt->generic.type == OMX_EVT_NONE) ;

  assert(evt->generic.type == OMX_EVT_PULL_DONE);
  assert(evt->pull_done.lib_cookie == COOKIE);
  printf("pull (cookie 0x%lx) transferred %ld bytes\n",
	 (unsigned long) evt->pull_done.lib_cookie,
	 (unsigned long) evt->pull_done.pulled_length);

  /* mark event as done */
  evt->generic.type = OMX_EVT_NONE;

  /* next event */
  evt++;
  if ((void *) evt >= exp_eventq + OMX_EXP_EVENTQ_SIZE)
    evt = exp_eventq;

  {
    int i;
    for(i=0; i<PULL_LENGTH; i++)
      if (send_buffer[i+SEND_OFFSET] != recv_buffer[i+RECV_OFFSET]) {
	printf("byte pulled different at #%d: '%c' instead of '%c'\n",
	       i, recv_buffer[i+RECV_OFFSET], send_buffer[i+SEND_OFFSET]);
	break;
      }
    for(i=0; i<RECV_OFFSET; i++)
      if (recv_buffer[i] != 'b') {
	printf("byte before those pulled different at #%d: '%c' instead of 'b'\n",
	       i, recv_buffer[i]);
	break;
      }
    for(i=RECV_OFFSET+PULL_LENGTH; i<BUFFER_LENGTH; i++)
      if (recv_buffer[i] != 'b') {
	printf("byte after those pulled different at #%d: '%c' instead of 'b'\n",
	       i, recv_buffer[i]);
	break;
      }
  }

  close(fd);

  return 0;

 out_with_recv_register:
  /* FIXME */
 out_with_send_register:
  /* FIXME */
 out_with_recv_buffer:
  free(recv_buffer);
 out_with_send_buffer:
  free(send_buffer);
 out_with_fd:
  close(fd);
 out:
  return -1;
}
