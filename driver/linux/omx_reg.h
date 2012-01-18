/*
 * Open-MX
 * Copyright © inria 2007-2009 (see AUTHORS file)
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

#ifndef __omx_region_h__
#define __omx_region_h__

#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/rcupdate.h>
#include <asm/processor.h>

#include "omx_common.h"
#include "omx_hal.h"

struct omx_endpoint;
struct sk_buff;

enum omx_user_region_status {
	OMX_USER_REGION_STATUS_NOT_PINNED,
	OMX_USER_REGION_STATUS_PINNED, /* or being pinned */
	OMX_USER_REGION_STATUS_FAILED,
};

struct omx_user_region {
	uint32_t id;

	unsigned dirty : 1;
	struct kref refcount;
	struct omx_endpoint *endpoint;

	struct rcu_head rcu_head; /* rcu deferred releasing callback */
	int nr_vmalloc_segments;
	struct work_struct destroy_work;

	unsigned nr_segments;
	unsigned long total_length;

	enum omx_user_region_status status;
	unsigned long total_registered_length;

	struct omx_user_region_segment {
		unsigned long aligned_vaddr;
		unsigned first_page_offset;
		unsigned long length;
		unsigned long nr_pages;
		unsigned long pinned_pages;
		int vmalloced;
		struct page ** pages;
	} segments[0];
};

struct omx_user_region_offset_cache {
	/* current segment and its offset */
	struct omx_user_region_segment *seg;
	unsigned long segoff;

	/* current page and its offset */
	struct page **page;
	unsigned int pageoff;

	/* region */
	struct omx_user_region *region;

	/* callbacks */
	int (*append_pages_to_skb) (struct omx_user_region_offset_cache * cache, struct sk_buff * skb, unsigned long length);
	void (*copy_pages_to_buf) (struct omx_user_region_offset_cache * cache, void *buffer, unsigned long length);
#ifdef OMX_HAVE_DMA_ENGINE
	int (*dma_memcpy_from_pg) (struct omx_user_region_offset_cache * cache, struct dma_chan *chan, dma_cookie_t *cookiep, struct page *page, int pgoff, unsigned long length);
	int (*dma_memcpy_from_buf) (struct omx_user_region_offset_cache * cache, struct dma_chan *chan, dma_cookie_t *cookiep, const void *buf, unsigned long length);
#endif

#ifdef OMX_DRIVER_DEBUG
	unsigned long current_offset;
	unsigned long max_offset;
#endif
};

extern void omx_endpoint_user_regions_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint);

extern int omx_ioctl_user_region_create(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_user_region_destroy(struct omx_endpoint * endpoint, void __user * uparam);

extern struct omx_user_region * omx_user_region_acquire(const struct omx_endpoint * endpoint, uint32_t rdma_id);
extern void __omx_user_region_last_release(struct kref * kref);

static inline void
omx_user_region_reacquire(struct omx_user_region * region)
{
	kref_get(&region->refcount);
}

static inline void
omx_user_region_release(struct omx_user_region * region)
{
	kref_put(&region->refcount, __omx_user_region_last_release);
}

extern int omx_user_region_offset_cache_init(struct omx_user_region *region, struct omx_user_region_offset_cache *cache, unsigned long offset, unsigned long length);
extern int omx_user_region_fill_pages(const struct omx_user_region * region, unsigned long region_offset, const struct sk_buff * skb, unsigned long length);
extern int omx_copy_between_user_regions(struct omx_user_region * src_region, unsigned long src_offset, struct omx_user_region * dst_region, unsigned long dst_offset, unsigned long length);

struct omx_user_region_pin_state {
	struct omx_user_region *region;
	struct omx_user_region_segment *segment; /* current segment */
	unsigned long aligned_vaddr; /* current aligned virtual address */
	unsigned long remaining; /* remaining length to pin in current segment */
	int chunk_offset; /* offset in current first page to pin */
	int watching; /* are we watching another guy doing the pinning? */
	int next_chunk_pages; /* number of pages to pin during next chunk */

	struct page **pages; /* current pages to setup */
	/* set to NULL when a new segment is being used */
};

/* internal routines */
extern void omx__user_region_pin_init(struct omx_user_region_pin_state *pinstate, struct omx_user_region *region);
extern int omx__user_region_pin_continue(struct omx_user_region_pin_state *pinstate, unsigned long *length);

/*
 * when demand-pinning is disabled,
 * do a regular full pinning early
 */
static inline int
omx_user_region_immediate_full_pin(struct omx_user_region * region)
{
	struct omx_user_region_pin_state pinstate;
	unsigned long needed = region->total_length;

#ifdef OMX_DRIVER_DEBUG
	BUG_ON(!omx_pin_synchronous);
	BUG_ON(region->status != OMX_USER_REGION_STATUS_NOT_PINNED);
#endif
	region->status = OMX_USER_REGION_STATUS_PINNED;

	omx__user_region_pin_init(&pinstate, region);
	return omx__user_region_pin_continue(&pinstate, &needed);
}

/*
 * when demand pinning is enabled,
 * wait for another guy to do enough pinning
 */
static inline int
omx_user_region_parallel_pin_wait(struct omx_user_region * region, unsigned long *length)
{
	unsigned long needed = *length;

#ifdef OMX_DRIVER_DEBUG
	BUG_ON(omx_pin_synchronous);
#endif

	while (needed > region->total_registered_length
	       && region->status == OMX_USER_REGION_STATUS_PINNED)
		cpu_relax();

	if (region->status == OMX_USER_REGION_STATUS_FAILED) {
		return -EFAULT;
	} else {
		*length = region->total_registered_length;
		return 0;
	}
}

/*
 * when demand pinning is enabled,
 * start an actually pinning,
 * or make sure another guy is pinning
 */
static inline void
omx_user_region_demand_pin_init(struct omx_user_region_pin_state *pinstate,
				struct omx_user_region * region)
{
#ifdef OMX_DRIVER_DEBUG
	BUG_ON(omx_pin_synchronous);
#endif

	if (cmpxchg(&region->status,
		    OMX_USER_REGION_STATUS_NOT_PINNED,
		    OMX_USER_REGION_STATUS_PINNED)) {
		/* somebody already registered this region */
		pinstate->region = region;
		pinstate->watching = 1;
	} else {
		/* start the pinning ourself */
		pinstate->watching = 0;
		omx__user_region_pin_init(pinstate, region);
	}

	/* let the status be checked by the actual user later */
}

/*
 * when demand pinning is enabled,
 * continue an actually pinning,
 * or wait until another guy progressed enough
 */
static inline int
omx_user_region_demand_pin_continue(struct omx_user_region_pin_state *pinstate,
				    unsigned long *length)
{
	struct omx_user_region *region = pinstate->region;

	if (pinstate->watching) {
		/* wait for the other guy to progress */
		return omx_user_region_parallel_pin_wait(region, length);

	} else {
		/* continue our pinning */
#ifdef OMX_DRIVER_DEBUG
		BUG_ON(omx_pin_synchronous);
		BUG_ON(region->status != OMX_USER_REGION_STATUS_PINNED);
#endif
		return omx__user_region_pin_continue(pinstate, length);
	}
}

/*
 * when demand pinning is enabled,
 * finish an actually pinning,
 * or wait until another guy finished
 */
static inline int
omx_user_region_demand_pin_finish(struct omx_user_region_pin_state *pinstate)
{
	struct omx_user_region *region = pinstate->region;
	unsigned long needed = region->total_length;

	if (pinstate->watching) {
		/* wait for the other guy to be done */
		return omx_user_region_parallel_pin_wait(region, &needed);

	} else {
		/* finish our pinning */
#ifdef OMX_DRIVER_DEBUG
		BUG_ON(omx_pin_synchronous);
		BUG_ON(pinstate->region->status != OMX_USER_REGION_STATUS_PINNED);
#endif
		return omx__user_region_pin_continue(pinstate, &needed);
	}
}

/*
 * when demand pinning is enabled,
 * finish an actually pinning,
 * or make sure another guy is pinning (without waiting for its completion)
 */
static inline int
omx_user_region_demand_pin_finish_or_parallel(struct omx_user_region_pin_state *pinstate)
{
	struct omx_user_region *region = pinstate->region;
	unsigned long needed = region->total_length;

	if (pinstate->watching) {
		/* let the other guy finish in parallel */
		return 0;

	} else {
		/* finish our pinning */
#ifdef OMX_DRIVER_DEBUG
		BUG_ON(omx_pin_synchronous);
		BUG_ON(pinstate->region->status != OMX_USER_REGION_STATUS_PINNED);
#endif
		return omx__user_region_pin_continue(pinstate, &needed);
	}
}

#endif /* __omx_region_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
