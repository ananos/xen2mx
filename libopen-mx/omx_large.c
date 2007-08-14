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

omx_return_t
omx__register_region(struct omx_endpoint *ep,
		     char * buffer, size_t length,
		     struct omx__large_region *region)
{
  struct omx_cmd_region_segment seg;
  struct omx_cmd_register_region reg;
  static uint8_t rdma_id = 0;
  uint8_t rdma_seqnum = 0; /* FIXME */
  uint32_t rdma_length;
  uint16_t offset;
  int err;

  offset = ((uintptr_t) buffer) & 4095;
  rdma_length = (offset + length + 4095) & ~4095;

  seg.vaddr = ((uintptr_t) buffer) & ~4095;
  seg.len = rdma_length;

  reg.nr_segments = 1;
  reg.id = rdma_id;
  reg.seqnum = rdma_seqnum;
  reg.memory_context = 0ULL; /* FIXME */
  reg.segments = (uintptr_t) &seg;

  err = ioctl(ep->fd, OMX_CMD_REGISTER_REGION, &reg);
  if (err < 0)
    return omx__errno_to_return("ioctl REGISTER");

  region->id = rdma_id++;
  region->seqnum = rdma_seqnum;
  region->offset = offset;
  return OMX_SUCCESS;
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

  return OMX_SUCCESS;
}

#define OMX__LARGE_REGION_COOKIE_MASK 0x25101984

static inline uint32_t
omx__large_region_cookie(struct omx__large_region * region)
{
  return OMX__LARGE_REGION_COOKIE_MASK ^ region->id;
}

static inline struct omx__large_region *
omx__large_region_from_cookie(uint32_t cookie)
{
  //uint8_t id = OMX__LARGE_REGION_COOKIE_MASK ^ cookie;

  /* FIXME */
  return NULL;
}

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

  region = &req->recv.specific.large.local_region;
  ret = omx__register_region(ep, req->recv.buffer, xfer_length, region);
  if (ret != OMX_SUCCESS)
    goto out;

  pull_param.dest_addr = partner->board_addr;
  pull_param.dest_endpoint = partner->endpoint_index;
  pull_param.length = xfer_length;
  pull_param.session_id = partner->session_id;
  pull_param.lib_cookie = omx__large_region_cookie(region);
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
  struct omx__partner * partner;
  struct omx_cmd_send_notify notify_param;
  omx_return_t ret;
  int err;

  req = omx__queue_first_request(&ep->large_recv_req_q);
  assert(req
	 && req->generic.type == OMX_REQUEST_TYPE_RECV_LARGE);
  /* FIXME: use event->lib_cookie to get region and then request */

  partner = req->generic.partner;
  /* FIXME: check length, update req->generic.status.xfer_length and status */

  omx__dequeue_request(&ep->large_recv_req_q, req);
  omx__deregister_region(ep, &req->recv.specific.large.local_region);

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

  req->generic.state = OMX_REQUEST_STATE_DONE;
  omx__enqueue_request(&ep->done_req_q, req);
  return OMX_SUCCESS;

 out:
  /* FIXME */
  assert(0);
  return ret;
}
