/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "omx_lib.h"

/************************
 * Send queue management
 */

static inline omx_return_t
omx__endpoint_sendq_map_init(struct omx_endpoint * ep)
{
  struct omx__sendq_entry * array;
  int i;

  array = malloc(OMX_SENDQ_ENTRY_NR * sizeof(struct omx__sendq_entry));
  if (!array)
    return omx__errno_to_return(ENOMEM, "sendq_map malloc");

  ep->sendq_map.array = array;

  for(i=0; i<OMX_SENDQ_ENTRY_NR; i++) {
    array[i].user = NULL;
    array[i].next_free = i+1;
  }
  array[OMX_SENDQ_ENTRY_NR-1].next_free = -1;
  ep->sendq_map.first_free = 0;
  ep->sendq_map.nr_free = OMX_SENDQ_ENTRY_NR;

  return OMX_SUCCESS;
}

static inline void
omx__endpoint_sendq_map_exit(struct omx_endpoint * ep)
{
  free(ep->sendq_map.array);
}

/**********************
 * Endpoint management
 */

omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t endpoint_index, uint32_t key,
		  struct omx_endpoint **epp)
{
  /* FIXME: add parameters to choose the board name? */
  struct omx_cmd_open_endpoint open_param;
  struct omx_endpoint * ep;
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  void * recvq, * sendq, * eventq;
  uint64_t board_addr;
  omx_return_t ret = OMX_SUCCESS;
  int err, fd;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    goto out;
  }

  ep = malloc(sizeof(struct omx_endpoint));
  if (!ep) {
    ret = omx__errno_to_return(ENOMEM, "endpoint malloc");
    goto out;
  }

  err = open(OMX_DEVNAME, O_RDWR);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "open");
    goto out_with_ep;
  }
  fd = err;

  /* ok */
  open_param.board_index = board_index;
  open_param.endpoint_index = endpoint_index;
  err = ioctl(fd, OMX_CMD_OPEN_ENDPOINT, &open_param);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl OPEN_ENDPOINT");
    goto out_with_fd;
  }

  /* prepare the sendq */
  ret = omx__endpoint_sendq_map_init(ep);
  if (ret != OMX_SUCCESS)
    goto out_with_attached;

  /* mmap */
  sendq = mmap(0, OMX_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_SENDQ_FILE_OFFSET);
  recvq = mmap(0, OMX_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_RECVQ_FILE_OFFSET);
  eventq = mmap(0, OMX_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_EVENTQ_FILE_OFFSET);
  if (sendq == (void *) -1
      || recvq == (void *) -1
      || eventq == (void *) -1) {
    ret = omx__errno_to_return(errno, "mmap");
    goto out_with_sendq_map;
  }
  printf("sendq at %p, recvq at %p, eventq at %p\n", sendq, recvq, eventq);

  /* init driver specific fields */
  ep->fd = fd;
  ep->sendq = sendq;
  ep->recvq = recvq;
  ep->eventq = ep->next_event = eventq;
  ep->board_index = board_index;
  ep->endpoint_index = endpoint_index;
  ep->session_id = open_param.session_id;
  ep->app_key = key;

  /* get some info */
  ret = omx__get_board_id(ep, NULL, ep->board_name, &board_addr);
  if (ret != OMX_SUCCESS)
    goto out_with_userq_mmap;

  omx__board_addr_sprintf(board_addr_str, board_addr);
  printf("Successfully attached endpoint #%ld on board #%ld (%s, %s)\n",
	 (unsigned long) endpoint_index, (unsigned long) board_index,
	 ep->board_name, board_addr_str);

  /* allocate partners */
  ep->partners = calloc(omx__globals.peer_max * omx__globals.endpoint_max,
			sizeof(*ep->partners));
  if (!ep->partners) {
    ret = omx__errno_to_return(ENOMEM, "endpoint partners calloc");
    goto out_with_userq_mmap;
  }

  /* connect to myself */
  ret = omx__connect_myself(ep, board_addr);
  if (ret != OMX_SUCCESS)
    goto out_with_partners;

  /* init lib specific fieds */
  INIT_LIST_HEAD(&ep->sent_req_q);
  INIT_LIST_HEAD(&ep->unexp_req_q);
  INIT_LIST_HEAD(&ep->recv_req_q);
  INIT_LIST_HEAD(&ep->multifraq_medium_recv_req_q);
  INIT_LIST_HEAD(&ep->connect_req_q);
  INIT_LIST_HEAD(&ep->done_req_q);

  *epp = ep;

  return OMX_SUCCESS;

 out_with_partners:
  free(ep->partners);
 out_with_userq_mmap:
  /* could munmap here, but close will do it */
 out_with_sendq_map:
  omx__endpoint_sendq_map_exit(ep);
 out_with_attached:
  /* could detach here, but close will do it */
 out_with_fd:
  close(fd);
 out_with_ep:
  free(ep);
 out:
  return ret;
}

omx_return_t
omx_close_endpoint(struct omx_endpoint *ep)
{
  munmap(ep->sendq, OMX_SENDQ_SIZE);
  munmap(ep->recvq, OMX_RECVQ_SIZE);
  munmap(ep->eventq, OMX_EVENTQ_SIZE);
  close(ep->fd);
  free(ep);

  return OMX_SUCCESS;
}
