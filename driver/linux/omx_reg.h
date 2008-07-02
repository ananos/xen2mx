/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

struct omx_endpoint;
struct sk_buff;

struct omx_user_region {
	uint32_t id;

	struct kref refcount;
	struct omx_endpoint *endpoint;

	struct rcu_head rcu_head; /* rcu deferred releasing callback */
	int nr_vmalloc_segments;
	struct list_head cleanup_list_elt; /* deferred cleanup thread freeing */

	unsigned nr_segments;
	unsigned long total_length;

	struct omx_user_region_segment {
		unsigned long aligned_vaddr;
		unsigned first_page_offset;
		unsigned long length;
		unsigned long nr_pages;
		int vmalloced;
		int pinned;
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
#ifdef CONFIG_NET_DMA
	int (*dma_memcpy_from_pg) (struct omx_user_region_offset_cache * cache, struct dma_chan *chan, dma_cookie_t *cookiep, struct page *page, int pgoff, unsigned long length);
	int (*dma_memcpy_from_buf) (struct omx_user_region_offset_cache * cache, struct dma_chan *chan, dma_cookie_t *cookiep, void *buf, unsigned long length);
#endif

#ifdef OMX_DEBUG
	unsigned long current_offset;
	unsigned long max_offset;
#endif
};

extern void omx_endpoint_user_regions_init(struct omx_endpoint * endpoint);
extern void omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint);

extern int omx_ioctl_user_region_create(struct omx_endpoint * endpoint, void __user * uparam);
extern int omx_ioctl_user_region_destroy(struct omx_endpoint * endpoint, void __user * uparam);

extern struct omx_user_region * omx_user_region_acquire(struct omx_endpoint * endpoint, uint32_t rdma_id);
extern void __omx_user_region_last_release(struct kref * kref);

extern void omx_user_regions_cleanup(void);

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
extern int omx_user_region_fill_pages(struct omx_user_region * region, unsigned long region_offset, struct sk_buff * skb, unsigned long length);
extern int omx_copy_between_user_regions(struct omx_user_region * src_region, unsigned long src_offset, struct omx_user_region * dst_region, unsigned long dst_offset, unsigned long length);

#endif /* __omx_region_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
