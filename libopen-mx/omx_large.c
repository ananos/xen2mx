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
    /* let the caller handle the error */
    return OMX_NO_RESOURCES;

  ep->large_region_map.array = array;

  for(i=0; i<OMX_USER_REGION_MAX; i++) {
    array[i].next_free = i+1;
    array[i].region.id = i;
    array[i].region.last_seqnum = 23;
  }
  array[OMX_USER_REGION_MAX-1].next_free = -1;
  ep->large_region_map.first_free = 0;
  ep->large_region_map.nr_free = OMX_USER_REGION_MAX;

  INIT_LIST_HEAD(&ep->reg_list);
  INIT_LIST_HEAD(&ep->reg_unused_list);
  INIT_LIST_HEAD(&ep->reg_vect_list);
  ep->large_sends_avail_nr = OMX_USER_REGION_MAX/2;

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__endpoint_large_region_try_alloc(struct omx_endpoint * ep,
				     struct omx__large_region ** regionp)
{
  struct omx__large_region_slot * array;
  int index, next_free;

  omx__debug_assert((ep->large_region_map.first_free == -1)
		    == (ep->large_region_map.nr_free == 0));

  index = ep->large_region_map.first_free;
  if (unlikely(index == -1))
    /* let the caller handle the error */
    return OMX_INTERNAL_MISSING_RESOURCES;

  array = ep->large_region_map.array;
  next_free = array[index].next_free;

  omx__debug_instr(array[index].next_free = -1);

  array[index].region.use_count = 0;
  *regionp = &array[index].region;

  ep->large_region_map.first_free = next_free;
  ep->large_region_map.nr_free--;

  return OMX_SUCCESS;
}

static INLINE void
omx__endpoint_large_region_free(struct omx_endpoint * ep,
				struct omx__large_region * region)
{
  struct omx__large_region_slot * array;
  int index = region->id;

  array = ep->large_region_map.array;

  omx__debug_assert(array[index].region.use_count == 0);
  omx__debug_assert(array[index].next_free == -1);

  array[index].next_free = ep->large_region_map.first_free;
  ep->large_region_map.first_free = index;
  ep->large_region_map.nr_free++;
}

static void omx__destroy_region(struct omx_endpoint *ep,  struct omx__large_region *region);

void
omx__endpoint_large_region_map_exit(struct omx_endpoint * ep)
{
  struct omx__large_region *region, *next;

  list_for_each_entry_safe(region, next, &ep->reg_list, reg_elt) {
    if (!region->use_count)
      list_del(&region->reg_unused_elt);
    omx__destroy_region(ep, region);
  }

  list_for_each_entry_safe(region, next, &ep->reg_vect_list, reg_elt) {
    omx__destroy_region(ep, region);
  }

  free(ep->large_region_map.array);
}

/****************************************
 * Low-level Registration/Deregistration
 */

static INLINE omx_return_t
omx__register_region(struct omx_endpoint *ep,
		     struct omx__large_region *region)
{
  struct omx_cmd_create_user_region reg;
  omx_return_t ret = OMX_SUCCESS;
  int err;

  reg.nr_segments = region->nseg;
  reg.id = region->id;
  reg.seqnum = 0; /* FIXME? unused since the driver can reuse a window multiple times */
  reg.memory_context = 0ULL; /* FIXME */
  reg.segments = (uintptr_t) region->segs;

  err = ioctl(ep->fd, OMX_CMD_CREATE_USER_REGION, &reg);
  if (unlikely(err < 0)) {
    omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
				       OMX_SUCCESS,
				       "create user region %d", region->id);
    ret = OMX_INTERNAL_MISSING_RESOURCES;
  }

  /* let the caller handle errors */
  return ret;
}

static INLINE void
omx__deregister_region(struct omx_endpoint *ep,
		       struct omx__large_region *region)
{
  struct omx_cmd_destroy_user_region dereg;
  int err;

  dereg.id = region->id;

  err = ioctl(ep->fd, OMX_CMD_DESTROY_USER_REGION, &dereg);
  if (unlikely(err < 0))
    omx__ioctl_errno_to_return_checked(OMX_SUCCESS, "destroy user region %d", region->id);
}

/***************************
 * Registration Cache Layer
 */

static void
omx__destroy_region(struct omx_endpoint *ep,
		    struct omx__large_region *region)
{
  omx__deregister_region(ep, region);
  list_del(&region->reg_elt);
  free(region->segs);
  omx__endpoint_large_region_free(ep, region);
}

static INLINE omx_return_t
omx__endpoint_large_region_alloc(struct omx_endpoint *ep, struct omx__large_region **regionp)
{
  omx_return_t ret;

  /* try once */
  ret = omx__endpoint_large_region_try_alloc(ep, regionp);

  if (unlikely(ret == OMX_INTERNAL_MISSING_RESOURCES && omx__globals.regcache)) {
    /* try to free some unused region in the cache */
    if (!list_empty(&ep->reg_unused_list)) {
      struct omx__large_region *region;
      region = list_first_entry(&ep->reg_unused_list, struct omx__large_region, reg_unused_elt);
      omx__debug_printf(LARGE, "regcache releasing unused region %d\n", region->id);
      list_del(&region->reg_unused_elt);
      omx__debug_printf(LARGE, "destroying region %d\n", region->id);
      omx__destroy_region(ep, region);

      /* try again now, it should work */
      ret = omx__endpoint_large_region_try_alloc(ep, regionp);
    }
  }

  /* let the caller handle errors */
  return ret;
}

static omx_return_t
omx__create_region(struct omx_endpoint *ep,
		   struct omx_cmd_user_region_segment *segs, uint32_t nseg,
		   uint16_t offset,
		   struct omx__large_region **regionp)
{
  struct omx__large_region *region = NULL;
  omx_return_t ret;

  ret = omx__endpoint_large_region_alloc(ep, &region);
  if (unlikely(ret != OMX_SUCCESS))
    /* let the caller handle the error */
    goto out;

  region->offset = offset;
  region->segs = segs;
  region->nseg = nseg;

  ret = omx__register_region(ep, region);
  if (ret != OMX_SUCCESS)
    /* let the caller handle the error */
    goto out_with_region;

  region->reserver = NULL;
  *regionp = region;
  return OMX_SUCCESS;

 out_with_region:
  omx__endpoint_large_region_free(ep, region);
 out:
  return ret;
}

static INLINE omx_return_t
omx__get_contigous_region(struct omx_endpoint *ep,
			  char * buffer, size_t length,
			  struct omx__large_region **regionp,
			  void *reserver)
{
  struct omx__large_region *region = NULL;
  struct omx_cmd_user_region_segment *rsegs;
  omx_return_t ret;
  uint64_t vaddr;
  uint64_t rdma_length;
  uint16_t offset;

  vaddr = ((uintptr_t) buffer) & ~4095;
  offset = ((uintptr_t) buffer) & 4095;
  rdma_length = ((uint64_t) offset + (uint64_t) length + 4095) & ~4095;

  if (reserver)
    omx__debug_printf(LARGE, "need a region reserved for object %p\n", reserver);
  else
    omx__debug_printf(LARGE, "need a region without reserving it\n");

  if (omx__globals.regcache) {
    list_for_each_entry(region, &ep->reg_list, reg_elt) {
      if ((!reserver || !region->reserver)
	  && (omx__globals.parallel_regcache || !region->use_count)
	  && region->segs[0].vaddr == vaddr
	  && region->segs[0].len >= rdma_length
	  && region->offset == offset) {

	if (!(region->use_count++))
	  list_del(&region->reg_unused_elt);
	omx__debug_printf(LARGE, "regcache reusing region %d (usecount %d)\n", region->id, region->use_count);
	goto found;
      }
    }
  }

  rsegs = malloc(sizeof(*rsegs));
  if (!rsegs) {
    ret = OMX_NO_RESOURCES;
    /* let the caller handle the error */
    goto out;
  }
  rsegs[0].vaddr = vaddr;
  rsegs[0].len = rdma_length;

  ret = omx__create_region(ep, rsegs, 1, offset, &region);
  if (ret != OMX_SUCCESS)
    /* let the caller handle the error */
    goto out_with_segments;

  list_add_tail(&region->reg_elt, &ep->reg_list);
  region->use_count++;
  omx__debug_printf(LARGE, "created contigous region %d (usecount %d)\n", region->id, region->use_count);

 found:
  if (reserver) {
    omx__debug_assert(!region->reserver);
    omx__debug_printf(LARGE, "reserving region %d for object %p\n", region->id, reserver);
    region->reserver = reserver;
  }

  *regionp = region;
  return OMX_SUCCESS;

 out_with_segments:
  free(rsegs);
 out:
  return ret;
}

static INLINE omx_return_t
omx__get_vect_region(struct omx_endpoint *ep,
		     struct omx__req_seg *reqsegs,
		     struct omx__large_region **regionp,
		     void *reserver)
{
  struct omx__large_region *region = NULL;
  struct omx_cmd_user_region_segment *segs;
  uint32_t nseg = reqsegs->nseg;
  omx_return_t ret;
  int i;

  if (reserver)
    omx__debug_printf(LARGE, "need a region reserved for object %p\n", reserver);
  else
    omx__debug_printf(LARGE, "need a region without reserving it\n");

  /* no regcache for vectorials */

  segs = malloc(sizeof(*segs) * nseg);
  if (!segs) {
    ret = OMX_NO_RESOURCES;
    /* let the caller handle the error */
    goto out;
  }

  for(i=0; i<nseg; i++) {
    omx_seg_t *seg = &reqsegs->segs[i];
    segs[i].vaddr = (uintptr_t) seg->ptr;
    segs[i].len = seg->len;
  }

  ret = omx__create_region(ep, segs, nseg, 0, &region);
  if (ret != OMX_SUCCESS)
    /* let the caller handle the error */
    goto out_with_segments;

  list_add_tail(&region->reg_elt, &ep->reg_vect_list);
  region->use_count++;
  omx__debug_printf(LARGE, "created vectorial region %d (usecount %d)\n", region->id, region->use_count);

  if (reserver) {
    omx__debug_assert(!region->reserver);
    omx__debug_printf(LARGE, "reserving region %d for object %p\n", region->id, reserver);
    region->reserver = reserver;
  }

  *regionp = region;
  return OMX_SUCCESS;

 out_with_segments:
  free(segs);
 out:
  return ret;
}

omx_return_t
omx__get_region(struct omx_endpoint *ep,
		struct omx__req_seg *reqsegs,
		struct omx__large_region **regionp,
		void *reserver)
{
  uint32_t nseg = reqsegs->nseg;
  if (nseg > 1) {
    return omx__get_vect_region(ep, reqsegs, regionp, reserver);
  } else {
    omx_seg_t *seg = &reqsegs->single;
    return omx__get_contigous_region(ep, seg->ptr, seg->len, regionp, reserver);
  }
}

omx_return_t
omx__put_region(struct omx_endpoint *ep,
		struct omx__large_region *region,
		void *reserver)
{
  region->use_count--;

  if (reserver) {
    omx__debug_assert(region->reserver == reserver);
    omx__debug_printf(LARGE, "unreserving region %d from object %p\n", region->id, reserver);
    region->reserver = NULL;
  }

  if (omx__globals.regcache && region->nseg == 1) {
    if (!region->use_count)
      list_add_tail(&region->reg_unused_elt, &ep->reg_unused_list);
    omx__debug_printf(LARGE, "regcache keeping region %d (usecount %d)\n", region->id, region->use_count);
  } else {
    omx__debug_printf(LARGE, "destroying region %d\n", region->id);
    omx__destroy_region(ep, region);
  }

  return OMX_SUCCESS;
}

/***************************
 * Large Messages Managment
 */

omx_return_t
omx__alloc_setup_pull(struct omx_endpoint * ep,
		      union omx_request * req)
{
  struct omx_cmd_pull pull_param;
  struct omx__large_region *region;
  uint32_t xfer_length = req->generic.status.xfer_length;
  struct omx__partner * partner = req->generic.partner;
  int res = req->generic.missing_resources;
  omx_return_t ret;
  int err;

  if (likely(res & OMX_REQUEST_RESOURCE_EXP_EVENT))
    goto need_exp_event;
  if (likely(res & OMX_REQUEST_RESOURCE_LARGE_REGION))
    goto need_region;
  if (likely(res & OMX_REQUEST_RESOURCE_PULL_HANDLE))
    goto need_pull;
  omx__abort(ep, "Unexpected missing resources %x for pull request\n", res);

 need_exp_event:
  if (unlikely(ep->avail_exp_events < 1))
    return OMX_INTERNAL_MISSING_RESOURCES;
  ep->avail_exp_events--;
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_EXP_EVENT;

 need_region:
  /* FIXME: could register xfer_length instead of the whole segments */
  ret = omx__get_region(ep, &req->recv.segs, &region, NULL);
  if (unlikely(ret != OMX_SUCCESS)) {
    omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
    return ret;
  }
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_LARGE_REGION;

 need_pull:
  pull_param.peer_index = partner->peer_index;
  pull_param.dest_endpoint = partner->endpoint_index;
  pull_param.shared = omx__partner_localization_shared(partner);
  pull_param.length = xfer_length;
  pull_param.session_id = partner->back_session_id;
  pull_param.lib_cookie = (uintptr_t) req;
  pull_param.local_rdma_id = region->id;
  pull_param.local_offset = region->offset;
  pull_param.remote_rdma_id = req->recv.specific.large.target_rdma_id;
  pull_param.remote_rdma_seqnum = req->recv.specific.large.target_rdma_seqnum;
  pull_param.remote_offset = req->recv.specific.large.target_rdma_offset;
  pull_param.resend_timeout_jiffies = ep->pull_resend_timeout_jiffies;

  err = ioctl(ep->fd, OMX_CMD_PULL, &pull_param);
  if (unlikely(err < 0)) {
    ret = omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
					     OMX_SUCCESS,
					     "post pull request");
    return OMX_INTERNAL_MISSING_RESOURCES;
  }
  req->generic.missing_resources &= ~OMX_REQUEST_RESOURCE_PULL_HANDLE;
  omx__debug_assert(!req->generic.missing_resources);

  req->recv.specific.large.local_region = region;
  req->generic.state |= OMX_REQUEST_STATE_DRIVER_PULLING;
  omx__enqueue_request(&ep->driver_pulling_req_q, req);

  return OMX_SUCCESS;
}

void
omx__submit_pull(struct omx_endpoint * ep,
		 union omx_request * req)
{
  omx_return_t ret;

  if (req->generic.status.xfer_length) {
    /* we need to pull some data */
    req->generic.missing_resources = OMX_REQUEST_PULL_RESOURCES;
    ret = omx__alloc_setup_pull(ep, req);
    if (unlikely(ret != OMX_SUCCESS)) {
      omx__debug_assert(ret == OMX_INTERNAL_MISSING_RESOURCES);
      omx__debug_printf(SEND, "queueing large request %p\n", req);
      req->generic.state |= OMX_REQUEST_STATE_NEED_RESOURCES;
      omx__enqueue_request(&ep->need_resources_send_req_q, req);
    }

  } else {
    /* nothing to transfer, just send the notify.
     * but we want to piggyack the rndv here too,
     * so we queue, let progression finish processing events,
     * and then send the notify as a queued request with correct piggyack
     */
    omx__debug_printf(LARGE, "large length 0, submitting request %p notify directly\n", req);
    req->generic.state &= ~OMX_REQUEST_STATE_RECV_PARTIAL;
    omx__submit_notify(ep, req, 1 /* always delayed */);
  }
}

void
omx__process_pull_done(struct omx_endpoint * ep,
		       struct omx_evt_pull_done * event)
{
  union omx_request * req;
  uintptr_t reqptr = event->lib_cookie;
  uint32_t region_id = event->local_rdma_id;
  struct omx__large_region * region;
  omx_return_t status;

  /* FIXME: use cookie since region might be used for something else? */
  req = (void *) reqptr;
  region = &ep->large_region_map.array[region_id].region;
  omx__debug_assert(req);
  omx__debug_assert(req->generic.type == OMX_REQUEST_TYPE_RECV_LARGE);
  omx__debug_assert(req->recv.specific.large.local_region == region);

  omx__debug_printf(LARGE, "pull done with status %d\n", event->status);

  switch (event->status) {
  case OMX_EVT_PULL_DONE_SUCCESS:
    status = OMX_SUCCESS;
    break;
  case OMX_EVT_PULL_DONE_BAD_ENDPT:
    status = OMX_REMOTE_ENDPOINT_BAD_ID;
    break;
  case OMX_EVT_PULL_DONE_ENDPT_CLOSED:
    status = OMX_REMOTE_ENDPOINT_CLOSED;
    break;
  case OMX_EVT_PULL_DONE_BAD_SESSION:
    status = OMX_REMOTE_ENDPOINT_BAD_SESSION;
    break;
  case OMX_EVT_PULL_DONE_BAD_RDMAWIN:
    status = OMX_REMOTE_RDMA_WINDOW_BAD_ID;
    break;
  case OMX_EVT_PULL_DONE_ABORTED:
    status = OMX_MESSAGE_ABORTED;
    break;
  case OMX_EVT_PULL_DONE_TIMEOUT:
    status = OMX_REMOTE_ENDPOINT_UNREACHABLE;
    break;
  default:
    omx__abort(ep, "Failed to handle NACK status %d\n",
	       event->status);
  }

  if (unlikely(status != OMX_SUCCESS)) {
    req->generic.status.code = omx__error_with_req(ep, req, status,
						   "Completing large receive request");
    req->generic.status.xfer_length = 0;
  }

  omx__put_region(ep, req->recv.specific.large.local_region, NULL);
  omx__dequeue_request(&ep->driver_pulling_req_q, req);
  req->generic.state &= ~(OMX_REQUEST_STATE_DRIVER_PULLING | OMX_REQUEST_STATE_RECV_PARTIAL);

  omx__submit_notify(ep, req, 0);
}

void
omx__process_recv_notify(struct omx_endpoint *ep, struct omx__partner *partner,
			 union omx_request *req /* ignored */,
			 struct omx_evt_recv_msg *msg,
			 void *data /* unused */, uint32_t msg_length /* unused */)
{
  uint32_t xfer_length = msg->specific.notify.length;
  uint8_t region_id = msg->specific.notify.puller_rdma_id;
  uint8_t region_seqnum = msg->specific.notify.puller_rdma_seqnum;
  struct omx__large_region * region;

  /* FIXME: check region id */
  region = &ep->large_region_map.array[region_id].region;
  req = region->reserver;

  if (region_seqnum != req->send.specific.large.region_seqnum)
    return;

  omx__debug_assert(req);
  omx__debug_assert(req->generic.type == OMX_REQUEST_TYPE_SEND_LARGE);
  omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_REPLY);

  omx__put_region(ep, req->send.specific.large.region, req);
  ep->large_sends_avail_nr++;

  req->generic.status.xfer_length = xfer_length;

  req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;
  if (req->generic.state & OMX_REQUEST_STATE_NEED_ACK) {
    /* keep the request in the non_acked_req_q */
  } else {
    omx__dequeue_request(&ep->large_send_need_reply_req_q, req);
    omx__send_complete(ep, req, OMX_SUCCESS);
  }

  if (omx__driver_desc->features & OMX_DRIVER_FEATURE_WIRECOMPAT) {
    /* MX < 1.2.5 needs an immediate ack for notify since it cannot mark
     * large recv as zombies.
     * But we can only do that if all previous seqnum are ready to be acked
     * too, which means next_frag == next_match.
     */
    if (partner->next_frag_recv_seq == partner->next_match_recv_seq)
      omx__mark_partner_need_ack_immediate(ep, partner);
  }
}
