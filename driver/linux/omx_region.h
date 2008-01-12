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

#ifndef __omx_region_h__
#define __omx_region_h__

#include <linux/spinlock.h>
#include <linux/kref.h>

struct omx_endpoint;
struct sk_buff;

struct omx_user_region {
	uint32_t id;

	struct kref refcount;

	unsigned nr_segments;
	unsigned long total_length;
	struct omx_user_region_segment {
		unsigned first_page_offset;
		unsigned long length;
		unsigned long nr_pages;
		struct page ** pages;
	} segments[0];
};

extern void omx_endpoint_user_regions_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint);
extern int omx_user_region_register(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_user_region_deregister(struct omx_endpoint * endpoint, void __user * uparam);
extern struct omx_user_region * omx_user_region_acquire(struct omx_endpoint * endpoint, uint32_t rdma_id);
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

extern void omx_user_region_release(struct omx_user_region * region);
extern int omx_user_region_append_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);
extern int omx_user_region_fill_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);

#endif /* __omx_region_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
