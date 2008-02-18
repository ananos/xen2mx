/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

#include "omx_lib.h"

/************************
 * Send queue management
 */

static INLINE omx_return_t
omx__endpoint_sendq_map_init(struct omx_endpoint * ep)
{
  struct omx__sendq_entry * array;
  int i;

  array = malloc(OMX_SENDQ_ENTRY_NR * sizeof(struct omx__sendq_entry));
  if (!array)
    return OMX_NO_RESOURCES;

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

static INLINE void
omx__endpoint_sendq_map_exit(struct omx_endpoint * ep)
{
  free(ep->sendq_map.array);
}

/**********************************
 * Find a board/endpoint available
 */

static INLINE omx_return_t
omx__open_one_endpoint(int fd,
		       uint32_t board_index, uint32_t endpoint_index)
{
  struct omx_cmd_open_endpoint open_param;
  int err;

  omx__debug_printf(ENDPOINT, "trying to open board #%d endpoint #%d\n",
		    board_index, endpoint_index);

  open_param.board_index = board_index;
  open_param.endpoint_index = endpoint_index;
  err = ioctl(fd, OMX_CMD_OPEN_ENDPOINT, &open_param);
  if (err < 0)
    return omx__errno_to_return("ioctl OPEN_ENDPOINT");

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__open_endpoint_in_range(int fd,
			    uint32_t board_start, uint32_t board_end,
			    uint32_t *board_found_p,
			    uint32_t endpoint_start, uint32_t endpoint_end,
			    uint32_t *endpoint_found_p)
{
  uint32_t board, endpoint;
  omx_return_t ret;
  int busy = 0;

  omx__debug_printf(ENDPOINT, "trying to open board [%d,%d] endpoint [%d,%d]\n",
		    board_start, board_end, endpoint_start, endpoint_end);

  /* loop on the board first, to distribute the load,
   * assuming no crappy board are attached (lo, ...)
   */
  for(endpoint=endpoint_start; endpoint<=endpoint_end; endpoint++)

    for(board=board_start; board<=board_end; board++) {

      /* try to open this one */
      ret = omx__open_one_endpoint(fd, board, endpoint);

      /* if success or error, return. if busy or nodev, try the next one */
      if (ret == OMX_SUCCESS) {
	omx__debug_printf(ENDPOINT, "successfully open board #%d endpoint #%d\n",
			  board, endpoint);
	*board_found_p = board;
	*endpoint_found_p = endpoint;
	return OMX_SUCCESS;
      } else if (ret != OMX_BUSY && ret != OMX_NO_DEVICE) {
	return ret;
      }

      if (ret == OMX_BUSY)
	busy++;
    }

  /* didn't find any endpoint available */
  return busy ? OMX_BUSY : OMX_NO_DEVICE;
}

static INLINE omx_return_t
omx__open_endpoint(int fd,
		   uint32_t *board_index_p, uint32_t *endpoint_index_p)
{
  uint32_t board_start, board_end;
  uint32_t endpoint_start, endpoint_end;

  if (*board_index_p == OMX_ANY_NIC) {
    board_start = 0;
    board_end = omx__driver_desc->board_max-1;
  } else {
    board_start = board_end = *board_index_p;
  }

  if (*endpoint_index_p == OMX_ANY_ENDPOINT) {
    endpoint_start = 0;
    endpoint_end = omx__driver_desc->endpoint_max-1;
  } else {
    endpoint_start = endpoint_end = *endpoint_index_p;
  }

  return omx__open_endpoint_in_range(fd,
				     board_start, board_end, board_index_p,
				     endpoint_start, endpoint_end, endpoint_index_p);
}

/**********************
 * Endpoint management
 */

/* API omx_open_endpoint */
omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t endpoint_index, uint32_t key,
		  omx_endpoint_param_t * param_array, uint32_t param_count,
		  struct omx_endpoint **epp)
{
  /* FIXME: add parameters to choose the board name? */
  struct omx_endpoint * ep;
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  struct omx_endpoint_desc * desc;
  void * recvq, * sendq, * exp_eventq, * unexp_eventq;
  uint8_t ctxid_bits = 0, ctxid_shift = 0;
  omx_return_t ret = OMX_SUCCESS;
  int err, fd, i;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    goto out;
  }

  for(i=0; i<param_count; i++) {
    switch (param_array[i].key) {
    case OMX_ENDPOINT_PARAM_ERROR_HANDLER:
      printf("setting endpoint error handler ignored for now\n");
      break;
    case OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX:
      printf("setting endpoint unexp queue max ignored for now\n");
      break;
    case OMX_ENDPOINT_PARAM_CONTEXT_ID:
      if (param_array[i].val.context_id.bits > OMX_ENDPOINT_CONTEXT_ID_BITS_MAX
          || param_array[i].val.context_id.bits + param_array[i].val.context_id.shift > 64) {
	ret = OMX_INVALID_PARAMETER;
	goto out;
      }
      ctxid_bits = param_array[i].val.context_id.bits;
      ctxid_shift = param_array[i].val.context_id.shift;
      break;
    default:
      ret = OMX_INVALID_PARAMETER;
      goto out;
    }
  }

  ep = malloc(sizeof(struct omx_endpoint));
  if (!ep) {
    ret = OMX_NO_RESOURCES;
    goto out;
  }
  omx__lock_init(&ep->lock);

  err = open(OMX_DEVNAME, O_RDWR);
  if (err < 0) {
    ret = omx__errno_to_return("open");
    goto out_with_ep;
  }
  fd = err;

  /* try to open */
  ret = omx__open_endpoint(fd, &board_index, &endpoint_index);
  if (ret != OMX_SUCCESS)
    goto out_with_fd;

  /* prepare the sendq */
  ret = omx__endpoint_sendq_map_init(ep);
  if (ret != OMX_SUCCESS)
    goto out_with_attached;

  /* mmap */
  desc = mmap(0, OMX_ENDPOINT_DESC_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_ENDPOINT_DESC_FILE_OFFSET);
  sendq = mmap(0, OMX_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_SENDQ_FILE_OFFSET);
  recvq = mmap(0, OMX_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_RECVQ_FILE_OFFSET);
  exp_eventq = mmap(0, OMX_EXP_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_EXP_EVENTQ_FILE_OFFSET);
  unexp_eventq = mmap(0, OMX_UNEXP_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_UNEXP_EVENTQ_FILE_OFFSET);

  if (desc == (void *) -1
      || sendq == (void *) -1
      || recvq == (void *) -1
      || exp_eventq == (void *) -1
      || unexp_eventq == (void *) -1) {
    ret = omx__errno_to_return("mmap");
    goto out_with_sendq_map;
  }
  omx__debug_printf(ENDPOINT, "desc at %p sendq at %p, recvq at %p, exp eventq at %p, unexp at %p\n",
		    desc, sendq, recvq, exp_eventq, unexp_eventq);

  /* prepare the large regions */
  ret = omx__endpoint_large_region_map_init(ep);
  if (ret != OMX_SUCCESS)
    goto out_with_userq_mmap;

  /* init driver specific fields */
  ep->fd = fd;
  ep->desc = desc;
  ep->sendq = sendq;
  ep->recvq = recvq;
  ep->exp_eventq = ep->next_exp_event = exp_eventq;
  ep->unexp_eventq = ep->next_unexp_event = unexp_eventq;
  ep->avail_exp_events = OMX_EXP_EVENTQ_ENTRY_NR;
  ep->board_index = board_index;
  ep->endpoint_index = endpoint_index;
  ep->app_key = key;
  ep->req_resends_max = omx__globals.req_resends_max;
  ep->pull_resend_timeout_jiffies = omx__globals.resend_delay_jiffies * omx__globals.req_resends_max;
  ep->check_status_delay_jiffies = omx__driver_desc->hz; /* once per second */
  ep->zombie_max = omx__globals.zombie_max;
  ep->zombies = 0;

  /* get some info */
  ret = omx__get_board_info(ep, -1, &ep->board_info);
  if (ret != OMX_SUCCESS)
    goto out_with_large_regions;

  omx__board_addr_sprintf(board_addr_str, ep->board_info.addr);
  omx__debug_printf(ENDPOINT, "Successfully attached endpoint #%ld on board #%ld (hostname '%s', name '%s', addr %s)\n",
		    (unsigned long) endpoint_index, (unsigned long) board_index,
		    ep->board_info.hostname, ep->board_info.ifacename, board_addr_str);

  /* allocate partners */
  ep->partners = calloc(omx__driver_desc->peer_max * omx__driver_desc->endpoint_max,
			sizeof(*ep->partners));
  if (!ep->partners) {
    ret = OMX_NO_RESOURCES;
    goto out_with_large_regions;
  }

  /* connect to myself */
  ret = omx__connect_myself(ep, ep->board_info.addr);
  if (ret != OMX_SUCCESS)
    goto out_with_partners;

  /* context id fields */
  ep->ctxid_bits = ctxid_bits;
  ep->ctxid_max = 1ULL << ctxid_bits;
  ep->ctxid_shift = ctxid_shift;
  ep->ctxid_mask = ((uint64_t) ep->ctxid_max - 1) << ctxid_shift;

  ep->ctxid = malloc(ep->ctxid_max * sizeof(*ep->ctxid));
  if (!ep->ctxid) {
    ret = OMX_NO_RESOURCES;
    goto out_with_myself;
  }
  for(i=0; i<ep->ctxid_max; i++) {
    INIT_LIST_HEAD(&ep->ctxid[i].unexp_req_q);
    INIT_LIST_HEAD(&ep->ctxid[i].recv_req_q);
    INIT_LIST_HEAD(&ep->ctxid[i].done_req_q);
  }

  /* init lib specific fieds */
  ep->unexp_handler = NULL;
  ep->in_handler = 0;

  INIT_LIST_HEAD(&ep->queued_send_req_q);
  INIT_LIST_HEAD(&ep->driver_posted_req_q);
  INIT_LIST_HEAD(&ep->multifrag_medium_recv_req_q);
  INIT_LIST_HEAD(&ep->large_send_req_q);
  INIT_LIST_HEAD(&ep->pull_req_q);
  INIT_LIST_HEAD(&ep->connect_req_q);
  INIT_LIST_HEAD(&ep->non_acked_req_q);
  INIT_LIST_HEAD(&ep->requeued_send_req_q);
  INIT_LIST_HEAD(&ep->send_self_unexp_req_q);

  INIT_LIST_HEAD(&ep->partners_to_ack_list);
  INIT_LIST_HEAD(&ep->throttling_partners_list);

  omx__progress(ep);

  *epp = ep;

  return OMX_SUCCESS;

 out_with_myself:
  free(ep->myself);
 out_with_partners:
  free(ep->partners);
 out_with_large_regions:
  omx__endpoint_large_region_map_exit(ep);
 out_with_userq_mmap:
  munmap(ep->desc, OMX_ENDPOINT_DESC_SIZE);
  munmap(ep->sendq, OMX_SENDQ_SIZE);
  munmap(ep->recvq, OMX_RECVQ_SIZE);
  munmap(ep->exp_eventq, OMX_EXP_EVENTQ_SIZE);
  munmap(ep->unexp_eventq, OMX_UNEXP_EVENTQ_SIZE);
 out_with_sendq_map:
  omx__endpoint_sendq_map_exit(ep);
 out_with_attached:
  /* nothing to do for detach, close will do it */
 out_with_fd:
  close(fd);
 out_with_ep:
  omx__lock_destroy(&ep->lock);
  free(ep);
 out:
  return ret;
}

/* API omx_close_endpoint */
omx_return_t
omx_close_endpoint(struct omx_endpoint *ep)
{
  omx_return_t ret;
  int i;

  OMX__ENDPOINT_LOCK(ep);

  if (ep->in_handler) {
    ret = OMX_NOT_SUPPORTED_IN_HANDLER;
    goto out_with_lock;
  }

  omx__flush_partners_to_ack(ep);

  free(ep->ctxid);
  for(i=0; i<omx__driver_desc->peer_max * omx__driver_desc->endpoint_max; i++)
    if (ep->partners[i])
      free(ep->partners[i]);
  free(ep->partners);
  omx__endpoint_large_region_map_exit(ep);
  munmap(ep->desc, OMX_ENDPOINT_DESC_SIZE);
  munmap(ep->sendq, OMX_SENDQ_SIZE);
  munmap(ep->recvq, OMX_RECVQ_SIZE);
  munmap(ep->exp_eventq, OMX_EXP_EVENTQ_SIZE);
  munmap(ep->unexp_eventq, OMX_UNEXP_EVENTQ_SIZE);
  omx__endpoint_sendq_map_exit(ep);
  /* nothing to do for detach, close will do it */
  close(ep->fd);
  omx__lock_destroy(&ep->lock);
  free(ep);

  return OMX_SUCCESS;

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}
