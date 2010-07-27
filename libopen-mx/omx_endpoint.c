/*
 * Open-MX
 * Copyright © INRIA 2007-2010 (see AUTHORS file)
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
#include <sched.h>

#include "omx_lib.h"
#include "omx_request.h"
#include "omx_segments.h"

static void
omx__destroy_requests_on_close(struct omx_endpoint *ep);

/***************************
 * Endpoint list management
 */

static struct list_head omx_endpoints_list;
#ifdef OMX_LIB_THREAD_SAFETY
static struct omx__lock omx_endpoints_list_lock;
#endif

static void
omx__init_endpoint_list(void)
{
  INIT_LIST_HEAD(&omx_endpoints_list);
  omx__lock_init(&omx_endpoints_list_lock);
}

static INLINE void
omx__add_endpoint_to_list(struct omx_endpoint *endpoint)
{
  omx__lock(&omx_endpoints_list_lock);
  list_add_tail(&endpoint->omx_endpoints_list_elt, &omx_endpoints_list);
  omx__unlock(&omx_endpoints_list_lock);
}

static INLINE omx_return_t
omx__remove_endpoint_from_list(struct omx_endpoint *endpoint)
{
  struct omx_endpoint *current;
  omx_return_t ret = OMX_BAD_ENDPOINT;

  omx__lock(&omx_endpoints_list_lock);

  list_for_each_entry(current, &omx_endpoints_list, omx_endpoints_list_elt)
    if (current == endpoint) {
      list_del(&endpoint->omx_endpoints_list_elt);
      ret = OMX_SUCCESS;
      break;
    }

  omx__unlock(&omx_endpoints_list_lock);

  /* let the caller handle errors */
  return ret;
}

void
omx__foreach_endpoint(void (*func)(struct omx_endpoint *, void *), void *data)
{
  struct omx_endpoint *current;

  omx__lock(&omx_endpoints_list_lock);
  list_for_each_entry(current, &omx_endpoints_list, omx_endpoints_list_elt)
    func(current, data);
  omx__unlock(&omx_endpoints_list_lock);
}

/************************
 * Send queue management
 */

static INLINE omx_return_t
omx__endpoint_sendq_map_init(struct omx_endpoint * ep)
{
  struct omx__sendq_entry * array;
  unsigned i;

  array = omx_malloc_ep(ep, OMX_SENDQ_ENTRY_NR * sizeof(struct omx__sendq_entry));
  if (!array)
    /* let the caller handle the error */
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
  omx_free(ep->sendq_map.array);
}

/**********
 * Binding
 */

static void
omx__endpoint_bind_process(const struct omx_endpoint *ep, const char *bindstring)
{
  cpu_set_t cs;
  CPU_ZERO(&cs);
  unsigned i;

  if (!strncmp(bindstring, "file", 4)) {
    char *filename;
    char line[OMX_PROCESS_BINDING_LENGTH_MAX];
    FILE *file;
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    unsigned long eid, irq;
    unsigned long long irqmask;

    filename = strchr(bindstring, ':');
    if (filename)
      filename++;
    else
      filename = OMX_PROCESS_BINDING_FILE;

    file = fopen(filename, "r");
    if (!file)
      omx__abort(ep, "Failed to open binding map %s, %m\n", filename);
    while (fgets(line, OMX_PROCESS_BINDING_LENGTH_MAX, file)) {
      if (sscanf(line, "board %s ep %ld irq %ld mask %llx", board_addr_str, &eid, &irq, &irqmask) == 4
	  && !strcmp(ep->board_addr_str, board_addr_str) && eid == ep->endpoint_index) {
        omx__verbose_printf(NULL, "Using binding %llx from file %s for process pid %ld with endpoint %d\n",
			    irqmask, filename, (unsigned long) getpid(), ep->endpoint_index);
	i=0;
	while (irqmask) {
	  if (irqmask & 1)
	    CPU_SET(i, &cs);
	  irqmask >>= 1;
	  i++;
        }
        sched_setaffinity(0, sizeof(cpu_set_t), &cs);
        break;
      }
    }
    fclose(file);

  } else {
    if (!strncmp(bindstring, "all:", 4)) {
      /* same binding whatever the endpoint */
      i = atoi(bindstring+4);
    } else {
      /* per endpoint binding */
      const char *c = bindstring;
      for(i=0; i<ep->endpoint_index; i++) {
	c = strchr(c, ',');
	if (!c)
	  break;
	c++;
      }
      if (!c)
	return;
      i = atoi(c);
    }

    CPU_SET(i, &cs);
    omx__verbose_printf(NULL, "Forcing binding on cpu #%d for process pid %ld with endpoint %d\n",
			i, (unsigned long) getpid(), ep->endpoint_index);
    sched_setaffinity(0, sizeof(cpu_set_t), &cs);
  }
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

  omx__debug_printf(ENDPOINT, NULL, "trying to open board #%d endpoint #%d\n",
		    board_index, endpoint_index);

  open_param.board_index = board_index;
  open_param.endpoint_index = endpoint_index;
  err = ioctl(fd, OMX_CMD_OPEN_ENDPOINT, &open_param);
  if (err < 0) {
    /* let the caller handle the error */
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
							  OMX_BUSY,
							  OMX_INTERNAL_MISC_EINVAL,
							  OMX_INTERNAL_MISC_ENODEV,
							  OMX_SUCCESS,
							  "open board #%d endpoint #%d",
							  board_index, endpoint_index);
    if (ret == OMX_INTERNAL_MISC_EINVAL)
      ret = OMX_BAD_ENDPOINT;
    else if (ret == OMX_INTERNAL_MISC_ENODEV)
      ret = OMX_BOARD_NOT_FOUND;
    return ret;
  }

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__open_endpoint_in_range(int fd,
			    uint32_t board_start, uint32_t board_end,
			    uint32_t * board_found_p,
			    uint32_t endpoint_start, uint32_t endpoint_end,
			    uint32_t * endpoint_found_p)
{
  uint32_t board, endpoint;
  omx_return_t ret;
  int busy = 0;

  omx__debug_printf(ENDPOINT, NULL, "trying to open board [%d,%d] endpoint [%d,%d]\n",
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
	omx__debug_printf(ENDPOINT, NULL, "successfully open board #%d endpoint #%d\n",
			  board, endpoint);
	*board_found_p = board;
	*endpoint_found_p = endpoint;
	return OMX_SUCCESS;
      } else if (ret != OMX_BUSY && ret != OMX_BOARD_NOT_FOUND) {
	/* let the caller handle the error */
	return ret;
      }

      if (ret == OMX_BUSY)
	busy++;
    }

  /* didn't find any endpoint available */
  return busy ? OMX_BUSY : OMX_BOARD_NOT_FOUND;
}

static INLINE omx_return_t
omx__open_endpoint(int fd,
		   uint32_t * board_index_p,
		   uint32_t * endpoint_index_p)
{
  uint32_t board_start, board_end;
  uint32_t endpoint_start, endpoint_end;

  if (*board_index_p == OMX_ANY_NIC) {
    board_start = 0;
    board_end = omx__driver_desc->board_max-1;
  } else {
    board_start = board_end = *board_index_p;
  }

  /* override OMX_ANY_ENDPOINT if needed */
  if (*endpoint_index_p == OMX_ANY_ENDPOINT)
    *endpoint_index_p = omx__globals.any_endpoint_id;

  if (*endpoint_index_p == OMX_ANY_ENDPOINT) {
    endpoint_start = 0;
    endpoint_end = omx__driver_desc->endpoint_max-1;
  } else {
    endpoint_start = endpoint_end = *endpoint_index_p;
  }

  return omx__open_endpoint_in_range(fd,
				     board_start, board_end, board_index_p,
				     endpoint_start, endpoint_end, endpoint_index_p);
  /* let the caller handle the error */
}

/*******
 * Misc
 */

static INLINE omx_return_t
omx__check_mmap(const char * string)
{
  omx_return_t ret = omx__errno_to_return();
  if (ret == OMX_INTERNAL_MISC_ENODEV || ret == OMX_INTERNAL_UNEXPECTED_ERRNO)
    return omx__error(OMX_BAD_ERROR, "Mapping %s (%m)", string);
  else
    return omx__error(ret, "Mapping %s", string);
}

/**********************
 * Endpoint management
 */

static int omx_comms_initialized = 0;

/* API omx_open_endpoint */
omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t endpoint_index, uint32_t key,
		  omx_endpoint_param_t * param_array, uint32_t param_count,
		  struct omx_endpoint **epp)
{
  /* FIXME: add parameters to choose the board name? */
  struct omx_endpoint * ep;
  struct omx_endpoint_desc * desc;
  void * recvq, * sendq, * exp_eventq, * unexp_eventq;
  uint8_t ctxid_bits;
  uint8_t ctxid_shift;
  omx_error_handler_t error_handler;
  omx_return_t ret = OMX_SUCCESS;
  int err, fd;
  unsigned i;

  if (!omx__globals.initialized) {
    ret = omx__error(OMX_NOT_INITIALIZED, "Opening endpoint");
    goto out;
  }

  if (!omx_comms_initialized) {
    omx__init_endpoint_list();
    omx__init_comms();
    omx_comms_initialized = 1;
  }

  if (param_count && !param_array) {
    ret = omx__error(OMX_ENDPOINT_PARAMS_BAD_LIST,
		     "Endpoint parameter list at NULL with %ld elements",
		     (unsigned long) param_count);
    goto out;
  }

  error_handler = NULL;
  ctxid_bits = omx__globals.ctxid_bits;
  ctxid_shift = omx__globals.ctxid_shift;

  for(i=0; i<param_count; i++) {
    switch (param_array[i].key) {
    case OMX_ENDPOINT_PARAM_ERROR_HANDLER: {
      error_handler = param_array[i].val.error_handler;
      break;
    }
    case OMX_ENDPOINT_PARAM_UNEXP_QUEUE_MAX: {
      omx__verbose_printf(NULL, "setting endpoint unexp queue max ignored for now\n");
      break;
    }
    case OMX_ENDPOINT_PARAM_CONTEXT_ID: {
      ctxid_bits = param_array[i].val.context_id.bits;
      ctxid_shift = param_array[i].val.context_id.shift;
      omx__verbose_printf(NULL, "Setting %d bits of context id at offset %d in matching\n",
			  ctxid_bits, ctxid_shift);
      break;
    }
    default: {
      ret = omx__error(OMX_ENDPOINT_PARAM_BAD_KEY,
		       "Reading endpoint parameter key %d", (unsigned) key);
      goto out;
    }
    }
  }

  if (ctxid_bits > OMX_ENDPOINT_CONTEXT_ID_BITS_MAX) {
    ret = omx__error(OMX_ENDPOINT_PARAM_BAD_VALUE,
		     "Opening Endpoint with %d ctxid bits",
		     (unsigned) ctxid_bits);
    goto out;
  }
  if (ctxid_bits + ctxid_shift > 64) {
    ret = omx__error(OMX_ENDPOINT_PARAM_BAD_VALUE,
		     "Opening Endpoint with %d ctxid bits at shift %d",
		     (unsigned) ctxid_bits, (unsigned) ctxid_shift);
    goto out;
  }

  ep = omx_malloc(sizeof(struct omx_endpoint));
  if (!ep) {
    ret = omx__error(OMX_NO_RESOURCES, "Allocating new endpoint");
    goto out;
  }

  err = open("/dev/" OMX_MAIN_DEVICE_NAME, O_RDWR);
  if (err < 0) {
    ret = omx__errno_to_return();
    if (ret == OMX_INTERNAL_UNEXPECTED_ERRNO)
      ret = omx__error(OMX_BAD_ERROR, "Opening endpoint control device (%m)");
    else if (ret == OMX_INTERNAL_MISC_ENODEV)
      ret = omx__error(OMX_NO_DRIVER, "Opening endpoint control device");
    else
      ret = omx__error(ret, "Opening endpoint control device");
    goto out_with_ep;
  }
  fd = err;

  /* try to open */
  ret = omx__open_endpoint(fd, &board_index, &endpoint_index);
  if (ret != OMX_SUCCESS) {
    ret = omx__error(ret, "Attaching endpoint to driver device");
    goto out_with_fd;
  }

  /* setup basic fields so that we can use ep in some subroutines */
  ep->fd = fd;
  ep->board_index = board_index;
  ep->endpoint_index = endpoint_index;
  ep->app_key = key;

  /* get some info */
  ret = omx__get_board_info(ep, -1, &ep->board_info);
  if (ret != OMX_SUCCESS) {
    ret = omx__error(ret, "Getting new endpoint board info");
    goto out_with_attached;
  }
  omx__board_addr_sprintf(ep->board_addr_str, ep->board_info.addr);

  /* bind the process if needed */
  if (omx__globals.process_binding)
    omx__endpoint_bind_process(ep, omx__globals.process_binding);

  /* prepare the sendq */
  ret = omx__endpoint_sendq_map_init(ep);
  if (ret != OMX_SUCCESS) {
    ret = omx__error(ret, "Initializing new endpoint send queue map");
    goto out_with_attached;
  }

  /* mmap desc */
  desc = mmap(0, OMX_ENDPOINT_DESC_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_ENDPOINT_DESC_FILE_OFFSET);
  if (desc == MAP_FAILED) {
    ret = omx__check_mmap("endpoint descriptor");
    goto out_with_sendq_map;
  }
  ep->desc = desc;
  /* mmap sendq */
  sendq = mmap(0, OMX_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_SENDQ_FILE_OFFSET);
  if (sendq == MAP_FAILED) {
    ret = omx__check_mmap("endpoint send queue");
    goto out_with_desc;
  }
  ep->sendq = sendq;
  /* mmap recvq */
  recvq = mmap(0, OMX_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_RECVQ_FILE_OFFSET);
  if (recvq == MAP_FAILED) {
    ret = omx__check_mmap("endpoint recv queue");
    goto out_with_sendq;
  }
  ep->recvq = recvq;
  /* mmap exp eventq */
  exp_eventq = mmap(0, OMX_EXP_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_EXP_EVENTQ_FILE_OFFSET);
  if (exp_eventq == MAP_FAILED) {
    ret = omx__check_mmap("endpoint expected event queue");
    goto out_with_recvq;
  }
  ep->exp_eventq = ep->next_exp_event = exp_eventq;
  /* mmap unexp eventq */
  unexp_eventq = mmap(0, OMX_UNEXP_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, OMX_UNEXP_EVENTQ_FILE_OFFSET);
  if (unexp_eventq == MAP_FAILED) {
    ret = omx__check_mmap("endpoint unexpected event queue");
    goto out_with_exp_eventq;
  }
  ep->unexp_eventq = ep->next_unexp_event = unexp_eventq;

  BUILD_BUG_ON(sizeof(struct omx_evt_recv_msg) != OMX_EVENTQ_ENTRY_SIZE);
  BUILD_BUG_ON(sizeof(union omx_evt) != OMX_EVENTQ_ENTRY_SIZE);

  omx__debug_printf(ENDPOINT, NULL, "desc at %p sendq at %p, recvq at %p, exp eventq at %p, unexp at %p\n",
		    desc, sendq, recvq, exp_eventq, unexp_eventq);
  omx__debug_printf(ENDPOINT, NULL, "Successfully attached endpoint #%ld on board #%ld (hostname '%s', name '%s', addr %s)\n",
		    (unsigned long) endpoint_index, (unsigned long) board_index,
		    ep->board_info.hostname, ep->board_info.ifacename, ep->board_addr_str);

  /* init most of the endpoint state */
  ep->avail_exp_events = OMX_EXP_EVENTQ_ENTRY_NR;
  ep->req_resends_max = omx__globals.req_resends_max;
  ep->pull_resend_timeout_jiffies = omx__globals.resend_delay_jiffies * omx__globals.req_resends_max;
  ep->check_status_delay_jiffies = omx__driver_desc->hz; /* once per second */
  ep->last_check_jiffies = 0;
#ifdef OMX_LIB_DEBUG
  ep->last_progress_jiffies = 0;
#endif
  ep->zombie_max = omx__globals.zombie_max;
  ep->zombies = 0;
  ep->error_handler = error_handler;
  ep->message_prefix = omx__create_message_prefix(ep); /* needs endpoint_index to be set */

  /* initialize some sub-structures */
  omx__request_alloc_init(ep);
  omx__lock_init(&ep->lock);
  omx__cond_init(&ep->in_handler_cond);

  /* prepare the large regions */
  ret = omx__endpoint_large_region_map_init(ep);
  if (ret != OMX_SUCCESS) {
    ret = omx__error(ret, "Initializing new endpoint large region map");
    goto out_with_message_prefix;
  }

  /* allocate partners */
  ep->partners = omx_calloc(omx__driver_desc->peer_max * omx__driver_desc->endpoint_max,
			    sizeof(*ep->partners));
  if (!ep->partners) {
    ret = omx__error(OMX_NO_RESOURCES, "Allocating new endpoint partners array");
    goto out_with_large_regions;
  }

  /* connect to myself */
  ret = omx__connect_myself(ep);
  if (ret != OMX_SUCCESS) {
    ret = omx__error(ret, "Connecting new endpoint to itself");
    goto out_with_partners;
  }

  /* context id fields */
  ep->ctxid_bits = ctxid_bits;
  ep->ctxid_max = 1ULL << ctxid_bits;
  ep->ctxid_shift = ctxid_shift;
  ep->ctxid_mask = ((uint64_t) ep->ctxid_max - 1) << ctxid_shift;

  ep->ctxid = omx_malloc_ep(ep, ep->ctxid_max * sizeof(*ep->ctxid));
  if (!ep->ctxid) {
    ret = omx__error(OMX_NO_RESOURCES, "Allocating new endpoint ctxids array");
    goto out_with_myself;
  }

  /* init lib specific fieds */
  ep->unexp_handler = NULL;
  ep->progression_disabled = 0;

  INIT_LIST_HEAD(&ep->anyctxid.done_req_q);
  INIT_LIST_HEAD(&ep->anyctxid.unexp_req_q);

  for(i=0; i<ep->ctxid_max; i++) {
    INIT_LIST_HEAD(&ep->ctxid[i].unexp_req_q);
    INIT_LIST_HEAD(&ep->ctxid[i].recv_req_q);
    INIT_LIST_HEAD(&ep->ctxid[i].done_req_q);
  }

  INIT_LIST_HEAD(&ep->need_resources_send_req_q);
  INIT_LIST_HEAD(&ep->driver_mediumsq_sending_req_q);
  INIT_LIST_HEAD(&ep->large_send_need_reply_req_q);
  INIT_LIST_HEAD(&ep->driver_pulling_req_q);
  INIT_LIST_HEAD(&ep->connect_req_q);
  INIT_LIST_HEAD(&ep->non_acked_req_q);
  INIT_LIST_HEAD(&ep->unexp_self_send_req_q);

#ifdef OMX_LIB_DEBUG
  INIT_LIST_HEAD(&ep->partial_medium_recv_req_q);
  INIT_LIST_HEAD(&ep->need_seqnum_send_req_q);
  INIT_LIST_HEAD(&ep->really_done_req_q);
  INIT_LIST_HEAD(&ep->internal_done_req_q);
#endif

  INIT_LIST_HEAD(&ep->partners_to_ack_immediate_list);
  ep->last_partners_acking_jiffies = 0;
  INIT_LIST_HEAD(&ep->partners_to_ack_delayed_list);
  INIT_LIST_HEAD(&ep->throttling_partners_list);

  INIT_LIST_HEAD(&ep->sleepers);

  ep->desc->user_event_index = 0;

  omx__add_endpoint_to_list(ep);

  omx__progress(ep);

  *epp = ep;

  return OMX_SUCCESS;

 out_with_myself:
  omx_free(ep->myself);
 out_with_partners:
  omx_free(ep->partners);
 out_with_large_regions:
  omx__endpoint_large_region_map_exit(ep);
 out_with_message_prefix:
  omx_free(ep->message_prefix);
  munmap(ep->exp_eventq, OMX_EXP_EVENTQ_SIZE);
 out_with_exp_eventq:
  munmap(ep->unexp_eventq, OMX_UNEXP_EVENTQ_SIZE);
 out_with_recvq:
  munmap(ep->recvq, OMX_RECVQ_SIZE);
 out_with_sendq:
  munmap(ep->sendq, OMX_SENDQ_SIZE);
 out_with_desc:
  munmap(ep->desc, OMX_ENDPOINT_DESC_SIZE);
 out_with_sendq_map:
  omx__endpoint_sendq_map_exit(ep);
 out_with_attached:
  /* nothing to do for detach, close will do it */
 out_with_fd:
  close(fd);
 out_with_ep:
  omx__lock_destroy(&ep->lock);
  omx__cond_destroy(&ep->in_handler_cond);
  omx_free(ep);
 out:
  return ret;
}

/* API omx_close_endpoint */
omx_return_t
omx_close_endpoint(struct omx_endpoint *ep)
{
  omx_return_t ret;
  unsigned i;

  OMX__ENDPOINT_LOCK(ep);

  if (ep->progression_disabled & OMX_PROGRESSION_DISABLED_IN_HANDLER) {
    ret = omx__error_with_ep(ep, OMX_NOT_SUPPORTED_IN_HANDLER, "Closing endpoint during unexpected handler");
    goto out_with_lock;
  }

  ret = omx__remove_endpoint_from_list(ep);
  if (ret != OMX_SUCCESS) {
    ret = omx__error(ret, "Closing endpoint");
    goto out_with_lock;
  }

  omx__flush_partners_to_ack(ep);

  omx__destroy_requests_on_close(ep);
  omx__request_alloc_check(ep);
  omx__request_alloc_exit(ep);

  omx_free(ep->ctxid);
  for(i=0; i<omx__driver_desc->peer_max * omx__driver_desc->endpoint_max; i++)
    if (ep->partners[i])
      omx_free(ep->partners[i]);
  omx_free(ep->partners);
  omx__endpoint_large_region_map_exit(ep);
  omx_free(ep->message_prefix);
  munmap(ep->unexp_eventq, OMX_UNEXP_EVENTQ_SIZE);
  munmap(ep->exp_eventq, OMX_EXP_EVENTQ_SIZE);
  munmap(ep->recvq, OMX_RECVQ_SIZE);
  munmap(ep->sendq, OMX_SENDQ_SIZE);
  munmap(ep->desc, OMX_ENDPOINT_DESC_SIZE);
  omx__endpoint_sendq_map_exit(ep);
  /* nothing to do for detach, close will do it */
  close(ep->fd);
  omx__lock_destroy(&ep->lock);
  omx__cond_destroy(&ep->in_handler_cond);
  omx_free(ep);

  return OMX_SUCCESS;

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}

/********************
 * Request releasing
 */

/* unlink the done queue elements if needed */
static INLINE void
omx__unlink_done_request_on_close(struct omx_endpoint *ep, union omx_request *req)
{
  if ((req->generic.state & OMX_REQUEST_STATE_DONE)
      && !(req->generic.state & OMX_REQUEST_STATE_ZOMBIE)) {
    list_del(&req->generic.done_elt);
    if (unlikely(HAS_CTXIDS(ep)))
      list_del(&req->generic.ctxid_elt);
  }
}

static void
omx__destroy_unlinked_request_on_close(struct omx_endpoint *ep, union omx_request *req)
{
  enum omx__request_type type = req->generic.type;
  int state = req->generic.state;
  int resources = req->generic.missing_resources;

  if (state == OMX_REQUEST_STATE_DONE)
    goto out;

  switch (type) {
  case OMX_REQUEST_TYPE_CONNECT:
    break;

  case OMX_REQUEST_TYPE_SEND_TINY:
    omx_free_segments(&req->send.segs);
    break;

  case OMX_REQUEST_TYPE_SEND_SMALL:
    omx_free(req->send.specific.small.copy);
    omx_free_segments(&req->send.segs);
    break;

  case OMX_REQUEST_TYPE_SEND_MEDIUMSQ:
    /* don't care about releasing sendq_map */
    omx_free_segments(&req->send.segs);
    break;

  case OMX_REQUEST_TYPE_SEND_MEDIUMVA:
    omx_free_segments(&req->send.segs);
    break;

  case OMX_REQUEST_TYPE_SEND_LARGE:
    if (!(resources & OMX_REQUEST_RESOURCE_LARGE_REGION)
	&& (state & OMX_REQUEST_STATE_NEED_REPLY))
      omx__put_region(ep, req->send.specific.large.region, req);
    omx_free_segments(&req->send.segs);
    break;

  case OMX_REQUEST_TYPE_RECV_LARGE:
    if (state & OMX_REQUEST_STATE_UNEXPECTED_RECV) {
      /* nothing to do */
    } else {
      if (!(resources & OMX_REQUEST_RESOURCE_LARGE_REGION)
	  && (state & OMX_REQUEST_STATE_RECV_PARTIAL))
	omx__put_region(ep, req->recv.specific.large.local_region, NULL);
      omx_free_segments(&req->send.segs);
    }
    break;

  case OMX_REQUEST_TYPE_RECV:
    if (state & OMX_REQUEST_STATE_UNEXPECTED_RECV) {
      if (req->generic.status.msg_length)
	omx_free(OMX_SEG_PTR(&req->recv.segs.single));
    } else {
      omx_free_segments(&req->send.segs);
    }
    break;

  case OMX_REQUEST_TYPE_SEND_SELF:
    omx_free_segments(&req->send.segs);
    break;

  case OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED:
    if (req->generic.status.msg_length)
      omx_free(OMX_SEG_PTR(&req->recv.segs.single));
    omx_free_segments(&req->send.segs);
    break;

  default:
    omx__abort(ep, "Failed to destroy request with type %d\n", req->generic.type);
  }

 out:
  /* no more resources to free */
  omx__request_free(ep, req);
}

static void
omx__destroy_requests_on_close(struct omx_endpoint *ep)
{
  union omx_request *req, *next;
  struct omx__early_packet *early, *next_early;
  unsigned i;

  for(i=0; i<omx__driver_desc->peer_max * omx__driver_desc->endpoint_max; i++) {
    struct omx__partner *partner =  ep->partners[i];
    if (!partner)
      continue;

    /* free early packets */
    omx__foreach_partner_early_packet_safe(partner, early, next_early) {
      omx___dequeue_partner_early_packet(early);
      omx_free(early->data);
      omx_free(early);
    }

    /* free throttling requests */
    omx__foreach_partner_request_safe(&partner->need_seqnum_send_req_q, req, next) {
      omx___dequeue_partner_request(req);
#ifdef OMX_LIB_DEBUG
      omx__dequeue_request(&ep->need_seqnum_send_req_q, req);
#endif
      /* cannot be done */
      omx__destroy_unlinked_request_on_close(ep, req);
    }

    /* free non-acked requests */
    omx__foreach_partner_request_safe(&partner->non_acked_req_q, req, next) {
      omx___dequeue_partner_request(req);
      /* the main request element is always queued when non-acked */
      omx___dequeue_request(req);
      omx__unlink_done_request_on_close(ep, req);
      omx__destroy_unlinked_request_on_close(ep, req);
    }

    /* free connect_req_q requests */
    omx__foreach_partner_request_safe(&partner->connect_req_q, req, next) {
      omx___dequeue_partner_request(req);
      omx__dequeue_request(&ep->connect_req_q, req);
      /* cannot be done */
      omx__destroy_unlinked_request_on_close(ep, req);
    }

    /* partial_medium_recv_req_q */
    omx__foreach_partner_request_safe(&partner->partial_medium_recv_req_q, req, next) {
      omx___dequeue_partner_request(req);
      /* cannot be done */
      omx__destroy_unlinked_request_on_close(ep, req);
    }
  }

  /* now that partner's queues are empty, some endpoint queues have to be empty as well */
  omx__debug_assert(omx__empty_queue(&ep->connect_req_q));
  omx__debug_assert(omx__empty_queue(&ep->non_acked_req_q));
#ifdef OMX_LIB_DEBUG
  omx__debug_assert(omx__empty_queue(&ep->need_seqnum_send_req_q));
  omx__debug_assert(omx__empty_queue(&ep->partial_medium_recv_req_q));
#endif

  /* free ctxid.recv and ctxid.unexp requests */
  for(i=0; i<ep->ctxid_max; i++) {
    omx__foreach_request_safe(&ep->ctxid[i].recv_req_q, req, next) {
      omx___dequeue_request(req);
      /* cannot be done */
      omx__destroy_unlinked_request_on_close(ep, req);
    }
  }

  /* free unexp reqs */
  omx__foreach_request_safe(&ep->anyctxid.unexp_req_q, req, next) {
    omx___dequeue_request(req);
    if (unlikely(HAS_CTXIDS(ep)))
      omx___dequeue_ctxid_request(req);
    /* cannot be done */
    omx__destroy_unlinked_request_on_close(ep, req);
  }

  /* free need_resources reqs */
  omx__foreach_request_safe(&ep->need_resources_send_req_q, req, next) {
    omx___dequeue_request(req);
    /* cannot be done */
    omx__destroy_unlinked_request_on_close(ep, req);
  }

  /* free driver_mediumsq_sending_req_q */
  omx__foreach_request_safe(&ep->driver_mediumsq_sending_req_q, req, next) {
    omx___dequeue_request(req);
    omx__unlink_done_request_on_close(ep, req);
    omx__destroy_unlinked_request_on_close(ep, req);
  }

  /* free large_send_need_reply_req_q */
  omx__foreach_request_safe(&ep->large_send_need_reply_req_q, req, next) {
    omx___dequeue_request(req);
    /* cannot be done */
    omx__destroy_unlinked_request_on_close(ep, req);
  }

  /* free driver_pulling_req_q */
  omx__foreach_request_safe(&ep->driver_pulling_req_q, req, next) {
    omx___dequeue_request(req);
    /* cannot be done */
    omx__destroy_unlinked_request_on_close(ep, req);
  }

  /* free unexp_self_send_req_q */
  omx__foreach_request_safe(&ep->unexp_self_send_req_q, req, next) {
    omx___dequeue_request(req);
    /* cannot be done */
    omx__destroy_unlinked_request_on_close(ep, req);
  }

#ifdef OMX_LIB_DEBUG
  /* there cannot be any internal requests anymore otherwise
   * it would mean that another thread is still using the endpoint
   * (it needs to destroy this request when he'll get the lock back)
   */
  omx__debug_assert(omx__empty_queue(&ep->internal_done_req_q));
#endif

  /* empty the anyctxid done queue. only really DONE requests are there since early done
   * requests have been dropped thanks to the partner need_seqnum_send_req_q already
   */
  omx__foreach_done_anyctxid_request_safe(ep, req, next) {
#ifdef OMX_LIB_DEBUG
    omx__debug_assert(req->generic.state == OMX_REQUEST_STATE_DONE);
    omx__dequeue_request(&ep->really_done_req_q, req);
#endif
    omx__unlink_done_request_on_close(ep, req);
    omx__destroy_unlinked_request_on_close(ep, req);
  }
  /* if ctxids, check that all ctxids queues are empty as well */
  if (unlikely(HAS_CTXIDS(ep)))
    for(i=0; i<ep->ctxid_max; i++)
      omx__debug_assert(omx__empty_done_ctxid_queue(ep, i));
#ifdef OMX_LIB_DEBUG
  /* check really_done_req_q is empty */
  omx__debug_assert(omx__empty_queue(&ep->really_done_req_q));
#endif
}

/***************************
 * Request Allocation Debug
 */
void
omx__request_alloc_check(const struct omx_endpoint *ep)
{
#ifdef OMX_LIB_DEBUG
  unsigned i, j, nr = 0;

  for(i=0; i<ep->ctxid_max; i++) {
    j = omx__queue_count(&ep->ctxid[i].recv_req_q);
    if (j > 0) {
      nr += j;
      if (omx__globals.check_request_alloc > 2)
        omx__verbose_printf(ep, "Found %d requests in recv queue #%d\n", j, i);
    }
  }

  j = omx__queue_count(&ep->anyctxid.unexp_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in unexp queue #%d\n", j, i);
  }

  j = omx__queue_count(&ep->need_resources_send_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in need-resources send queue\n", j);
  }

  j = omx__queue_count(&ep->need_seqnum_send_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in need-seqnum send queue\n", j);
  }

  j = omx__queue_count(&ep->driver_mediumsq_sending_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in driver mediumsq sending queue\n", j);
  }

  j = omx__queue_count(&ep->partial_medium_recv_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in partial medium recv queue\n", j);
  }

  j = omx__queue_count(&ep->large_send_need_reply_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in large send need-reply queue\n", j);
  }

  j = omx__queue_count(&ep->driver_pulling_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in driver pulling queue\n", j);
  }

  j = omx__queue_count(&ep->connect_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in connect queue\n", j);
  }

  j = omx__queue_count(&ep->non_acked_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in non-acked queue\n", j);
  }

  j = omx__queue_count(&ep->unexp_self_send_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in large send self unexp queue\n", j);
  }

  j = omx__queue_count(&ep->really_done_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in really done queue\n", j);
  }

  j = omx__queue_count(&ep->internal_done_req_q);
  if (j > 0) {
    nr += j;
    if (omx__globals.check_request_alloc > 2)
      omx__verbose_printf(ep, "Found %d requests in internal done queue\n", j);
  }

  if (nr != ep->req_alloc_nr || omx__globals.check_request_alloc > 1)
    omx__verbose_printf(ep, "Found %d requests in queues for %d allocations\n", nr, ep->req_alloc_nr);
  if (nr != ep->req_alloc_nr)
    omx__abort(ep, "%d requests out of %d missing in endpoint queues\n", ep->req_alloc_nr - nr, ep->req_alloc_nr);
#endif
}
