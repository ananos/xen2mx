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

omx_return_t
omx__endpoint_large_region_map_init(struct omx_endpoint * ep)
{
  struct omx__large_region_slot * array;
  int i;

  array = malloc(OMX_USER_REGION_MAX * sizeof(struct omx__large_region_slot));
  if (!array)
    return OMX_NO_RESOURCES;

  ep->large_region_map.array = array;

  for(i=0; i<OMX_USER_REGION_MAX; i++) {
    array[i].next_free = i+1;
    array[i].region.id = i;
    array[i].region.user = NULL;
    /* FIXME: seqnum */
  }
  array[OMX_USER_REGION_MAX-1].next_free = -1;
  ep->large_region_map.first_free = 0;
  ep->large_region_map.nr_free = OMX_USER_REGION_MAX;

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
  if (unlikely(index == -1))
    return OMX_NO_RESOURCES;

  array = ep->large_region_map.array;
  next_free = array[index].next_free;

  omx__debug_assert(array[index].region.user == NULL);
  omx__debug_instr(array[index].next_free = -1);

  array[index].region.use_count = 0;
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

  omx__debug_assert(array[index].region.use_count == 0);
  omx__debug_assert(array[index].next_free == -1);

  array[index].region.user = NULL;
  array[index].next_free = ep->large_region_map.first_free;
  ep->large_region_map.first_free = index;
  ep->large_region_map.nr_free++;
}

/****************************************
 * Low-level Registration/Deregistration
 */

static omx_return_t
omx__register_region(struct omx_endpoint *ep,
		     struct omx__large_region *region)
{
  struct omx_cmd_register_region reg;
  int err;

  reg.nr_segments = 1;
  reg.id = region->id;
  reg.seqnum = region->seqnum;
  reg.memory_context = 0ULL; /* FIXME */
  reg.segments = (uintptr_t) region->segs;

  err = ioctl(ep->fd, OMX_CMD_REGISTER_REGION, &reg);
  if (unlikely(err < 0))
    return omx__errno_to_return("ioctl REGISTER");

  return OMX_SUCCESS;
}

static omx_return_t
omx__deregister_region(struct omx_endpoint *ep,
		       struct omx__large_region *region)
{
  struct omx_cmd_deregister_region dereg;
  int err;

  dereg.id = region->id;

  err = ioctl(ep->fd, OMX_CMD_DEREGISTER_REGION, &dereg);
  if (unlikely(err < 0))
    return omx__errno_to_return("ioctl REGISTER");

  return OMX_SUCCESS;
}

/***************************
 * Registration Cache Layer
 */

static void
omx__destroy_region(struct omx_endpoint *ep,
		    struct omx__large_region *region)
{
  omx_return_t ret;

  ret = omx__deregister_region(ep, region);
  assert(ret == OMX_SUCCESS);

  list_del(&region->regcache_elt);
  free(region->segs);
  omx__endpoint_large_region_free(ep, region);
}

static omx_return_t
omx__create_region(struct omx_endpoint *ep,
		   uint64_t vaddr, uint32_t rdma_length, uint16_t offset,
		   struct omx__large_region **regionp)
{
  struct omx__large_region *region = NULL;
  struct omx_cmd_region_segment *segs;
  omx_return_t ret;

  ret = omx__endpoint_large_region_alloc(ep, &region);
  if (unlikely(ret == OMX_NO_RESOURCES
	       && omx__globals.regcache)) {
    /* try to free some unused region in the cache */
    if (!list_empty(&ep->regcache_unused_list)) {
      region = list_first_entry(&ep->regcache_unused_list, struct omx__large_region, regcache_unused_elt);
      omx__debug_printf("regcache releasing unused region %d\n", region->id);
      list_del(&region->regcache_unused_elt);
      omx__destroy_region(ep, region);
      ret = omx__endpoint_large_region_alloc(ep, &region);
    }
  }
  if (unlikely(ret != OMX_SUCCESS))
    goto out;

  segs = malloc(sizeof(*segs));
  if (!segs) {
    ret = omx__errno_to_return("alloc register segments");
    goto out_with_region;
  }

  segs[0].vaddr = vaddr;
  segs[0].len = rdma_length;

  region->offset = offset;
  region->segs = segs;

  ret = omx__register_region(ep, region);
  if (ret != OMX_SUCCESS)
    goto out_with_segments;

  list_add_tail(&region->regcache_elt, &ep->regcache_list);
  *regionp = region;
  return OMX_SUCCESS;

 out_with_segments:
  free(segs);
 out_with_region:
  omx__endpoint_large_region_free(ep, region);
 out:
  return ret;
}

omx_return_t
omx__get_region(struct omx_endpoint *ep,
		char * buffer, size_t length,
		struct omx__large_region **regionp)
{
  struct omx__large_region *region = NULL;
  uint64_t vaddr;
  uint32_t rdma_length;
  uint16_t offset;
  omx_return_t ret;

  vaddr = ((uintptr_t) buffer) & ~4095;
  offset = ((uintptr_t) buffer) & 4095;
  rdma_length = (offset + length + 4095) & ~4095;

  if (omx__globals.regcache) {
    list_for_each_entry(region, &ep->regcache_list, regcache_elt) {
      if (region->segs[0].vaddr == vaddr
	  && region->segs[0].len >= rdma_length
	  && region->offset == offset) {

	if (!(region->use_count++))
	  list_del(&region->regcache_unused_elt);
	*regionp = region;
	return OMX_SUCCESS;
      }
    }
  }

  ret = omx__create_region(ep, vaddr, rdma_length, offset, &region);
  if (ret != OMX_SUCCESS)
    return ret;

  region->use_count++;
  *regionp = region;
  return OMX_SUCCESS;
}

omx_return_t
omx__put_region(struct omx_endpoint *ep,
		struct omx__large_region *region)
{
  region->use_count--;
  if (omx__globals.regcache) {
    if (!region->use_count)
      list_add_tail(&region->regcache_unused_elt, &ep->regcache_unused_list);
  } else {
    omx__destroy_region(ep, region);
  }

  return OMX_SUCCESS;
}

/***************************
 * Large Messages Managment
 */

omx_return_t
omx__post_pull(struct omx_endpoint * ep,
	       union omx_request * req)
{
  struct omx_cmd_send_pull pull_param;
  struct omx__large_region *region;
  uint32_t xfer_length = req->generic.status.xfer_length;
  struct omx__partner * partner = req->generic.partner;
  omx_return_t ret;
  int err;

  if (unlikely(ep->avail_exp_events < 1))
    return OMX_NO_RESOURCES;

  ret = omx__get_region(ep, req->recv.buffer, xfer_length, &region);
  if (unlikely(ret != OMX_SUCCESS))
    return ret;

  pull_param.peer_index = partner->peer_index;
  pull_param.dest_endpoint = partner->endpoint_index;
  pull_param.length = xfer_length;
  pull_param.session_id = partner->session_id;
  /* FIXME: cookie */
  pull_param.local_rdma_id = region->id;
  pull_param.local_offset = region->offset;
  /* FIXME: seqnum */
  pull_param.remote_rdma_id = req->recv.specific.large.target_rdma_id;
  pull_param.remote_offset = req->recv.specific.large.target_rdma_offset;
  pull_param.retransmit_delay_jiffies = omx__globals.resend_delay * omx__globals.retransmits_max;

  err = ioctl(ep->fd, OMX_CMD_SEND_PULL, &pull_param);
  if (unlikely(err < 0)) {
    ret = omx__errno_to_return("ioctl SEND_PULL");
    if (ret != OMX_NO_SYSTEM_RESOURCES) {
      /* FIXME: error message, something went wrong in the driver */
      assert(0);
    }

    omx__put_region(ep, region);
    return ret;
  }
  ep->avail_exp_events--;

  region->user = req;
  req->recv.specific.large.local_region = region;
  req->generic.state |= OMX_REQUEST_STATE_IN_DRIVER;
  omx__enqueue_request(&ep->pull_req_q, req);

  return OMX_SUCCESS;
}

omx_return_t
omx__submit_pull(struct omx_endpoint * ep,
		 union omx_request * req)
{
  omx_return_t ret;

  ret = omx__post_pull(ep, req);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_printf("queueing large request %p\n", req);
    req->generic.state |= OMX_REQUEST_STATE_QUEUED;
    omx__enqueue_request(&ep->queued_send_req_q, req);
  }

  return OMX_SUCCESS;
}

omx_return_t
omx__process_pull_done(struct omx_endpoint * ep,
		       struct omx_evt_pull_done * event)
{
  union omx_request * req;
  uint32_t xfer_length = event->pulled_length;
  uint32_t region_id = event->local_rdma_id;
  struct omx__large_region * region;
  struct omx__partner * partner;
  struct omx_cmd_send_notify notify_param;
  omx_status_code_t status;
  omx__seqnum_t seqnum;
  omx_return_t ret;
  int err;

  /* FIXME: use cookie since region might be used for something else? */

  omx__debug_printf("pull done with status %d\n", event->status);

  /* FIXME: check region id */
  region = &ep->large_region_map.array[region_id].region;
  req = region->user;
  assert(req);
  assert(req->generic.type == OMX_REQUEST_TYPE_RECV_LARGE);

  partner = req->generic.partner;
  /* FIXME: check length, update req->generic.status.xfer_length and status */

  omx__dequeue_request(&ep->pull_req_q, req);
  omx__put_region(ep, req->recv.specific.large.local_region);

  seqnum = partner->next_send_seq;

  notify_param.peer_index = partner->peer_index;
  notify_param.dest_endpoint = partner->endpoint_index;
  notify_param.total_length = xfer_length;
  notify_param.session_id = partner->session_id;
  notify_param.seqnum = seqnum;
  notify_param.piggyack = partner->next_frag_recv_seq - 1;
  notify_param.puller_rdma_id = req->recv.specific.large.target_rdma_id;
  notify_param.puller_rdma_seqnum = req->recv.specific.large.target_rdma_seqnum;

  err = ioctl(ep->fd, OMX_CMD_SEND_NOTIFY, &notify_param);
  if (unlikely(err < 0)) {
    ret = omx__errno_to_return("ioctl SEND_NOTIFY");
    goto out;
  }

  /* increase at the end, to avoid having to decrease back in case of error */
  partner->next_send_seq++;
  omx__partner_ack_sent(ep, partner);

  switch (event->status) {
  case OMX_EVT_PULL_DONE_SUCCESS:
    status = OMX_STATUS_SUCCESS;
    break;
  case OMX_EVT_PULL_DONE_BAD_ENDPT:
    status = OMX_STATUS_BAD_ENDPOINT;
    break;
  case OMX_EVT_PULL_DONE_ENDPT_CLOSED:
    status = OMX_STATUS_ENDPOINT_CLOSED;
    break;
  case OMX_EVT_PULL_DONE_BAD_SESSION:
    status = OMX_STATUS_BAD_SESSION;
    break;
  case OMX_EVT_PULL_DONE_BAD_RDMAWIN:
    status = OMX_STATUS_BAD_RDMAWIN;
    break;
  case OMX_EVT_PULL_DONE_ABORTED:
    status = OMX_STATUS_ABORTED;
    break;
  case OMX_EVT_PULL_DONE_TIMEOUT:
    status = OMX_STATUS_ENDPOINT_UNREACHABLE;
    break;
  default:
    assert(0);
    break;
  }

  req->generic.send_seqnum = seqnum;
  req->generic.state &= ~(OMX_REQUEST_STATE_IN_DRIVER | OMX_REQUEST_STATE_RECV_PARTIAL);
  req->generic.state |= OMX_REQUEST_STATE_NEED_ACK;
  omx__enqueue_request(&ep->non_acked_req_q, req);
  omx__enqueue_partner_non_acked_request(partner, req);

  return OMX_SUCCESS;

 out:
  /* FIXME */
  assert(0);
  return ret;
}

void
omx__process_recv_notify(struct omx_endpoint *ep, struct omx__partner *partner,
			 union omx_request *req /* ignored */,
			 struct omx_evt_recv_msg *msg,
			 void *data /* unused */, uint32_t msg_length /* unused */)
{
  uint32_t xfer_length = msg->specific.notify.length;
  uint8_t region_id = msg->specific.notify.puller_rdma_id;
  struct omx__large_region * region;

  /* FIXME: check region id */
  region = &ep->large_region_map.array[region_id].region;
  req = region->user;
  assert(req);
  assert(req->generic.type == OMX_REQUEST_TYPE_SEND_LARGE);
  assert(req->generic.state & OMX_REQUEST_STATE_NEED_REPLY);
  omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_NEED_ACK));
  /* there should be a ack in the notify message, and we processed it before coming here */

  omx__dequeue_request(&ep->large_send_req_q, req);
  omx__put_region(ep, req->send.specific.large.region);
  req->generic.status.xfer_length = xfer_length;

  req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;
  omx__send_complete(ep, req, OMX_STATUS_SUCCESS);
}
