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

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "omx_io.h"
#include "omx_lib.h"
#include "omx_request.h"

/***********************
 * Region Map managment
 */

#define OMX__LARGE_REGION_ID_MAX 256

omx_return_t
omx__endpoint_large_region_map_init(struct omx_endpoint * ep)
{
  struct omx__large_region_slot * array;
  int i;

  array = malloc(OMX__LARGE_REGION_ID_MAX * sizeof(struct omx__large_region_slot));
  if (!array)
    return OMX_NO_RESOURCES;

  ep->large_region_map.array = array;

  for(i=0; i<OMX__LARGE_REGION_ID_MAX; i++) {
    array[i].next_free = i+1;
    array[i].region.id = i;
    array[i].region.user = NULL;
    /* FIXME: seqnum */
  }
  array[OMX__LARGE_REGION_ID_MAX-1].next_free = -1;
  ep->large_region_map.first_free = 0;
  ep->large_region_map.nr_free = OMX__LARGE_REGION_ID_MAX;

  return OMX_SUCCESS;
}

void
omx__endpoint_large_region_map_exit(struct omx_endpoint * ep)
{
  /* FIXME: check and deregister */
  free(ep->large_region_map.array);
}

static inline omx_return_t
omx__endpoint_large_region_alloc(struct omx_endpoint * ep,
				 struct omx__large_region ** regionp)
{
  struct omx__large_region_slot * array;
  int index, next_free;

  assert((ep->large_region_map.first_free == -1)
	 == (ep->large_region_map.nr_free == 0));

  index = ep->large_region_map.first_free;
  if (index == -1)
    return OMX_NO_RESOURCES;

  array = ep->large_region_map.array;
  next_free = array[index].next_free;

  omx__debug_assert(array[index].region.user == NULL);
  omx__debug_instr(array[index].next_free = -1);

  *regionp = &array[index].region;

  ep->large_region_map.first_free = next_free;
  ep->large_region_map.nr_free--;

  return OMX_SUCCESS;
}

static inline void
omx__endpoint_large_region_free(struct omx_endpoint * ep,
				struct omx__large_region * region)
{
  struct omx__large_region_slot * array;
  int index = region->id;

  array = ep->large_region_map.array;

  omx__debug_assert(array[index].next_free == -1);

  array[index].region.user = NULL;
  array[index].next_free = ep->large_region_map.first_free;
  ep->large_region_map.first_free = index;
  ep->large_region_map.nr_free++;
}

/******************************
 * Registration/Deregistration
 */

omx_return_t
omx__register_region(struct omx_endpoint *ep,
		     char * buffer, size_t length,
		     struct omx__large_region **regionp)
{
  struct omx_cmd_region_segment seg;
  struct omx_cmd_register_region reg;
  uint32_t rdma_length;
  uint16_t offset;
  omx_return_t ret;
  int err;

  ret = omx__endpoint_large_region_alloc(ep, regionp);
  if (ret != OMX_SUCCESS)
    goto out;

  offset = ((uintptr_t) buffer) & 4095;
  rdma_length = (offset + length + 4095) & ~4095;
  (*regionp)->offset = offset;

  seg.vaddr = ((uintptr_t) buffer) & ~4095;
  seg.len = rdma_length;

  reg.nr_segments = 1;
  reg.id = (*regionp)->id;
  reg.seqnum = (*regionp)->seqnum;
  reg.memory_context = 0ULL; /* FIXME */
  reg.segments = (uintptr_t) &seg;

  err = ioctl(ep->fd, OMX_CMD_REGISTER_REGION, &reg);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl REGISTER");
    goto out_with_region;
  }

  return OMX_SUCCESS;

 out_with_region:
  omx__endpoint_large_region_free(ep, *regionp);
 out:
  return ret;
}

omx_return_t
omx__deregister_region(struct omx_endpoint *ep,
		       struct omx__large_region *region)
{
  struct omx_cmd_deregister_region dereg;
  int err;

  dereg.id = region->id;

  err = ioctl(ep->fd, OMX_CMD_DEREGISTER_REGION, &dereg);
  if (err < 0)
    return omx__errno_to_return("ioctl REGISTER");

  omx__endpoint_large_region_free(ep, region);

  return OMX_SUCCESS;
}

/***************************
 * Large Messages Managment
 */

omx_return_t
omx__queue_large_recv(struct omx_endpoint * ep,
		      union omx_request * req)
{
  struct omx_cmd_send_pull pull_param;
  struct omx__large_region *region;
  uint32_t xfer_length = req->generic.status.xfer_length;
  struct omx__partner * partner = req->generic.partner;
  omx_return_t ret;
  int err;

  ret = omx__register_region(ep, req->recv.buffer, xfer_length, &region);
  if (ret != OMX_SUCCESS)
    goto out;

  pull_param.dest_addr = partner->board_addr;
  pull_param.dest_endpoint = partner->endpoint_index;
  pull_param.length = xfer_length;
  pull_param.session_id = partner->session_id;
  /* FIXME: cookie */
  pull_param.local_rdma_id = region->id;
  pull_param.local_offset = region->offset;
  /* FIXME: seqnum */
  pull_param.remote_rdma_id = req->recv.specific.large.target_rdma_id;
  pull_param.remote_offset = req->recv.specific.large.target_rdma_offset;

  err = ioctl(ep->fd, OMX_CMD_SEND_PULL, &pull_param);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl SEND_PULL");
    goto out_with_reg;
  }

  region->user = req;
  req->recv.specific.large.local_region = region;
  omx__enqueue_request(&ep->large_recv_req_q, req);

  return OMX_SUCCESS;

 out_with_reg:
  omx__deregister_region(ep, region);
 out:
  return ret;
}

omx_return_t
omx__pull_done(struct omx_endpoint * ep,
	       struct omx_evt_pull_done * event)
{
  union omx_request * req;
  uint32_t xfer_length = event->pulled_length;
  uint32_t region_id = event->local_rdma_id;
  struct omx__large_region * region;
  struct omx__partner * partner;
  struct omx_cmd_send_notify notify_param;
  omx_return_t ret;
  int err;

  /* FIXME: use cookie since region might be used for something else? */
  region = &ep->large_region_map.array[region_id].region;
  req = region->user;
  assert(req);
  assert(req->generic.type == OMX_REQUEST_TYPE_RECV_LARGE);

  partner = req->generic.partner;
  /* FIXME: check length, update req->generic.status.xfer_length and status */

  omx__dequeue_request(&ep->large_recv_req_q, req);
  omx__deregister_region(ep, req->recv.specific.large.local_region);

  notify_param.dest_addr = partner->board_addr;
  notify_param.dest_endpoint = partner->endpoint_index;
  notify_param.total_length = xfer_length;
  notify_param.session_id = partner->session_id;
  /* FIXME: seqnum */
  notify_param.puller_rdma_id = req->recv.specific.large.target_rdma_id;
  notify_param.puller_rdma_seqnum = req->recv.specific.large.target_rdma_seqnum;

  err = ioctl(ep->fd, OMX_CMD_SEND_NOTIFY, &notify_param);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl SEND_NOTIFY");
    goto out;
  }

  req->generic.state &= ~OMX_REQUEST_STATE_PENDING;
  req->generic.state |= OMX_REQUEST_STATE_DONE;
  omx__recv_complete(ep, req, OMX_STATUS_SUCCESS);

  return OMX_SUCCESS;

 out:
  /* FIXME */
  assert(0);
  return ret;
}
