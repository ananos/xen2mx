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

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

#include "omx_hal.h"
#include "omx_io.h"
#include "omx_common.h"
#include "omx_endpoint.h"
#include "omx_iface.h"
#include "omx_reg.h"
#include "omx_dma.h"

#ifdef OMX_MX_WIRE_COMPAT
#if OMX_USER_REGION_MAX > 256
#error Cannot store region id > 255 in 8bit id on the wire
#endif
#endif

/******************************
 * Add and Destroying segments
 */

#define OMX_REGION_VMALLOC_NR_PAGES_THRESHOLD 4096

static int
omx_user_region_add_segment(struct omx_cmd_user_region_segment * useg,
			    struct omx_user_region_segment * segment)
{
	unsigned long usegvaddr = useg->vaddr;
	unsigned long useglen = useg->len;
	struct page ** pages;
	unsigned offset;
	unsigned long aligned_vaddr;
	unsigned long aligned_len;
	unsigned long nr_pages;
	int ret;

	offset = usegvaddr & (~PAGE_MASK);
	aligned_vaddr = usegvaddr & PAGE_MASK;
	aligned_len = PAGE_ALIGN(offset + useglen);
	nr_pages = aligned_len >> PAGE_SHIFT;

	if (nr_pages > OMX_REGION_VMALLOC_NR_PAGES_THRESHOLD) {
		pages = vmalloc(nr_pages * sizeof(struct page *));
		segment->vmalloced = 1;
	} else {
		pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
		segment->vmalloced = 0;
	}
	if (unlikely(!pages)) {
		printk(KERN_ERR "Open-MX: Failed to allocate user region segment page array\n");
		ret = -ENOMEM;
		goto out;
	}

#ifdef OMX_DEBUG
	memset(pages, 0, nr_pages * sizeof(struct page *));
#endif

	segment->aligned_vaddr = aligned_vaddr;
	segment->first_page_offset = offset;
	segment->length = useglen;
	segment->nr_pages = nr_pages;
	segment->pinned_pages = 0;
	segment->pages = pages;

	return 0;

 out:
	return ret;
}

static void
omx_user_region_destroy_segment(struct omx_user_region_segment * segment)
{
	unsigned long i;

	for(i=0; i<segment->pinned_pages; i++)
		put_page(segment->pages[i]);

	if (segment->vmalloced)
		vfree(segment->pages);
	else
		kfree(segment->pages);
}

static void
omx_user_region_destroy_segments(struct omx_user_region * region)
{
	int i;

	if (region->nr_vmalloc_segments)
		might_sleep();

	for(i=0; i<region->nr_segments; i++)
		omx_user_region_destroy_segment(&region->segments[i]);
}

/*****************
 * Region pinning
 */

void
omx__user_region_pin_init(struct omx_user_region_pin_state *pinstate,
			  struct omx_user_region *region)
{
	pinstate->region = region;
	pinstate->segment = &region->segments[0];
	pinstate->pages = NULL; /* means that pin_new_segment() will do the init soon */
	pinstate->aligned_vaddr = 0;
	pinstate->remaining = 0;
	pinstate->chunk_offset = 0;
	pinstate->next_chunk_pages = omx_pin_chunk_pages_min;
}

static inline void
omx__user_region_pin_new_segment(struct omx_user_region_pin_state *pinstate)
{
	/*
	 * Called when pages is NULL, meaning that we finished the previous segment.
	 * The caller that set pages to NULL and increased the segment did not do this
	 * because it didn't know whether the next segment was valid. Now that we are
	 * here, we know it is valid since we are pinning more memory.
	 */
	struct omx_user_region_segment *segment = pinstate->segment;
	pinstate->aligned_vaddr = segment->aligned_vaddr;
	pinstate->pages = segment->pages;
	pinstate->remaining = segment->length;
	pinstate->chunk_offset = segment->first_page_offset;
}

static int
omx__user_region_pin_add_chunk(struct omx_user_region_pin_state *pinstate)
{
	struct omx_user_region *region = pinstate->region;
	struct omx_user_region_segment *seg = pinstate->segment;
	unsigned long aligned_vaddr;
	struct page ** pages;
	unsigned long remaining;
	int chunk_offset;
	int chunk_length;
	int chunk_pages;
	int ret;

	if (!pinstate->pages)
		omx__user_region_pin_new_segment(pinstate);
	aligned_vaddr = pinstate->aligned_vaddr;
	pages = pinstate->pages;
	remaining = pinstate->remaining;
	chunk_offset = pinstate->chunk_offset;

	/* compute an estimated number of pages to pin */
	chunk_pages = pinstate->next_chunk_pages;
	/* increase the next number of pages to pin if below the max */
	if (chunk_pages < omx_pin_chunk_pages_max) {
		chunk_pages <<= 1;
		if (chunk_pages > omx_pin_chunk_pages_max)
			chunk_pages = omx_pin_chunk_pages_max;
		pinstate->next_chunk_pages = chunk_pages;
	}

	/* compute the corresponding length */
	if (chunk_offset + remaining <= chunk_pages<<PAGE_SHIFT)
		chunk_length = remaining;
	else
		chunk_length = (chunk_pages<<PAGE_SHIFT) - chunk_offset;

	/* compute the actual corresponding number of pages to pin */
	chunk_pages = (chunk_offset + chunk_length + PAGE_SIZE-1) >> PAGE_SHIFT;

	ret = get_user_pages(current, current->mm, aligned_vaddr, chunk_pages, 1, 0, pages, NULL);
	if (unlikely(ret < 0)) {
		printk(KERN_ERR "Open-MX: get_user_pages failed (error %d)\n", ret);
		goto out;
	}
#ifdef OMX_DEBUG
	BUG_ON(ret != chunk_pages);
#endif

	seg->pinned_pages += chunk_pages;
	region->total_registered_length += chunk_length;
	barrier(); /* needed for busy-waiter on total_registered_length */

	if (chunk_length < remaining) {
		/* keep the same segment */
		pinstate->aligned_vaddr = aligned_vaddr + chunk_offset + chunk_length;
		pinstate->pages = pages + chunk_pages;
		pinstate->remaining = remaining - chunk_length;
		pinstate->chunk_offset = 0;

	} else {
		/* jump to next segment */
#ifdef OMX_DEBUG
		BUG_ON(seg->pinned_pages != seg->nr_pages);
#endif
		pinstate->pages = NULL;
		pinstate->segment = seg + 1;
	}

	return 0;

 out:
	return ret;
}

int
omx__user_region_pin_continue(struct omx_user_region_pin_state *pinstate,
			      unsigned long *length)
{
	struct omx_user_region *region = pinstate->region;
	unsigned long needed = *length;
	int ret;

#ifdef OMX_DEBUG
	BUG_ON(region->status != OMX_USER_REGION_STATUS_PINNED);
#endif

	down_write(&current->mm->mmap_sem);
	while (region->total_registered_length < needed) {
		ret = omx__user_region_pin_add_chunk(pinstate);
		if (ret < 0)
			goto out;
	}
	up_write(&current->mm->mmap_sem);
	*length = region->total_registered_length;
	return 0;

 out:
	up_write(&current->mm->mmap_sem);
	region->status = OMX_USER_REGION_STATUS_FAILED;
	return ret;
}

/******************
 * Region creation
 */

int
omx_ioctl_user_region_create(struct omx_endpoint * endpoint,
			     void __user * uparam)
{
	struct omx_cmd_create_user_region cmd;
	struct omx_user_region * region;
	struct omx_user_region_segment *seg;
	struct omx_cmd_user_region_segment * usegs;
	int ret, i;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read create region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.id >= OMX_USER_REGION_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot create invalid region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}

	/* get the list of segments */
	usegs = kmalloc(sizeof(struct omx_cmd_user_region_segment) * cmd.nr_segments,
			GFP_KERNEL);
	if (unlikely(!usegs)) {
		printk(KERN_ERR "Open-MX: Failed to allocate segments for user region\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = copy_from_user(usegs, (void __user *)(unsigned long) cmd.segments,
			     sizeof(struct omx_cmd_user_region_segment) * cmd.nr_segments);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read create region cmd\n");
		ret = -EFAULT;
		goto out_with_usegs;
	}

	/* allocate the region */
	region = kzalloc(sizeof(struct omx_user_region)
			 + cmd.nr_segments * sizeof(struct omx_user_region_segment),
			 GFP_KERNEL);
	if (unlikely(!region)) {
		printk(KERN_ERR "Open-MX: failed to allocate user region\n");
		ret = -ENOMEM;
		goto out_with_usegs;
	}

	kref_init(&region->refcount);
	region->total_length = 0;
	region->nr_vmalloc_segments = 0;

	/* keep nr_segments exact so that we may call omx_user_region_destroy_segments safely */
	region->nr_segments = 0;

	/* allocate all segments */
	for(i=0, seg = &region->segments[0]; i<cmd.nr_segments; i++) {
		dprintk(REG, "create region looking at useg %d len %lld\n",
			i, (unsigned long long) usegs[i].len);
		if (!usegs[i].len)
			continue;
		ret = omx_user_region_add_segment(&usegs[i], seg);
		if (unlikely(ret < 0))
			goto out_with_region;

		if (seg->vmalloced)
			region->nr_vmalloc_segments++;
		region->nr_segments++;
		region->total_length += seg->length;
		dprintk(REG, "create region added new seg #%ld, total %ld length %ld\n",
			(unsigned long) (seg-&region->segments[0]),
			(unsigned long) region->nr_segments, region->total_length);
		seg++;
	}

	/* mark the region as non-registered yet */
	region->status = OMX_USER_REGION_STATUS_NOT_PINNED;
	region->total_registered_length = 0;

	if (!omx_region_demand_pin) {
		/* pin the region */
		ret = omx_user_region_immediate_full_pin(region);
		if (ret < 0) {
			dprintk(REG, "failed to pin user region\n");
			goto out_with_region;
		}
	}

	spin_lock(&endpoint->user_regions_lock);

	if (unlikely(endpoint->user_regions[cmd.id] != NULL)) {
		printk(KERN_ERR "Open-MX: Cannot create busy region %d\n", cmd.id);
		ret = -EBUSY;
		spin_unlock(&endpoint->user_regions_lock);
		goto out_with_region;
	}

	region->endpoint = endpoint;
	region->id = cmd.id;
	rcu_assign_pointer(endpoint->user_regions[cmd.id], region);

	spin_unlock(&endpoint->user_regions_lock);

	kfree(usegs);
	return 0;

 out_with_region:
	omx_user_region_destroy_segments(region);
	kfree(region);
 out_with_usegs:
	kfree(usegs);
 out:
	return ret;
}

/********************
 * Region destroying
 */

/* vfree cannot be called from BH, so we just
 * let the cleanup thread take care of it by moving
 * regions to a cleanup list first
 */
static spinlock_t omx_regions_cleanup_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(omx_regions_cleanup_list);

/* Called when the last reference on the region is released */
void
__omx_user_region_last_release(struct kref * kref)
{
	struct omx_user_region * region = container_of(kref, struct omx_user_region, refcount);

	dprintk(KREF, "releasing the last reference on region %p\n",
		region);

	if (region->nr_vmalloc_segments) {
		spin_lock_bh(&omx_regions_cleanup_lock);
		list_add_tail(&region->cleanup_list_elt, &omx_regions_cleanup_list);
		spin_unlock_bh(&omx_regions_cleanup_lock);
	} else {
		omx_user_region_destroy_segments(region);
		kfree(region);
	}
}

void
omx_user_regions_cleanup(void)
{
        struct omx_user_region * region, * next;
	LIST_HEAD(private_head);

	/* move the whole list to our private head at once */
	spin_lock_bh(&omx_regions_cleanup_lock);
	list_splice(&omx_regions_cleanup_list, &private_head);
	INIT_LIST_HEAD(&omx_regions_cleanup_list);
	spin_unlock_bh(&omx_regions_cleanup_lock);

	/* and now free all regions without needing any lock */
	list_for_each_entry_safe(region, next, &private_head, cleanup_list_elt) {
		omx_user_region_destroy_segments(region);
		list_del(&region->cleanup_list_elt);
		kfree(region);
	}
}

static void
__omx_user_region_rcu_release_callback(struct rcu_head *rcu_head)
{
	struct omx_user_region * region = container_of(rcu_head, struct omx_user_region, rcu_head);
	kref_put(&region->refcount, __omx_user_region_last_release);
}

int
omx_ioctl_user_region_destroy(struct omx_endpoint * endpoint,
			      void __user * uparam)
{
	struct omx_cmd_destroy_user_region cmd;
	struct omx_user_region * region;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read destroy region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	ret = -EINVAL;
	if (unlikely(cmd.id >= OMX_USER_REGION_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot destroy invalid region %d\n", cmd.id);
		goto out;
	}

	spin_lock(&endpoint->user_regions_lock);

	region = endpoint->user_regions[cmd.id];
	if (unlikely(!region)) {
		printk(KERN_ERR "Open-MX: Cannot destroy unexisting region %d\n", cmd.id);
		goto out_with_endpoint_lock;
	}

	rcu_assign_pointer(endpoint->user_regions[cmd.id], NULL);
	/*
	 * since synchronize_rcu() is too expensive in this critical path,
	 * just defer the actual releasing after the grace period
	 */
	call_rcu(&region->rcu_head, __omx_user_region_rcu_release_callback);

	spin_unlock(&endpoint->user_regions_lock);
	return 0;

 out_with_endpoint_lock:
	spin_unlock(&endpoint->user_regions_lock);
 out:
	return ret;
}

/******************************
 * User Region Acquire/Release
 */

/* maybe be called from bottom halves */
struct omx_user_region *
omx_user_region_acquire(struct omx_endpoint * endpoint,
			uint32_t rdma_id)
{
	struct omx_user_region * region;

	if (unlikely(rdma_id >= OMX_USER_REGION_MAX))
		goto out;

	rcu_read_lock();

	region = rcu_dereference(endpoint->user_regions[rdma_id]);
	if (unlikely(!region))
		goto out_with_rcu_lock;

	kref_get(&region->refcount);

	rcu_read_unlock();
	return region;

 out_with_rcu_lock:
	rcu_read_unlock();
 out:
	return NULL;
}

/***************************************
 * Endpoint User Regions Initialization
 */

void
omx_endpoint_user_regions_init(struct omx_endpoint * endpoint)
{
	memset(endpoint->user_regions, 0, sizeof(endpoint->user_regions));
	spin_lock_init(&endpoint->user_regions_lock);
}

void
omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint)
{
	struct omx_user_region * region;
	int i;

	spin_lock(&endpoint->user_regions_lock);

	for(i=0; i<OMX_USER_REGION_MAX; i++) {
		region = endpoint->user_regions[i];
		if (!region)
			continue;

		dprintk(REG, "forcing destroy of window %d on endpoint %d board %d\n",
			i, endpoint->endpoint_index, endpoint->board_index);

		rcu_assign_pointer(endpoint->user_regions[i], NULL);
		/* just defer the actual releasing after the grace period */
		call_rcu(&region->rcu_head, __omx_user_region_rcu_release_callback);
	}

	spin_unlock(&endpoint->user_regions_lock);
}

/*********************************
 * Appending region pages to send
 */

static int
omx_user_region_offset_cache_contig_append_callback(struct omx_user_region_offset_cache * cache,
						    struct sk_buff * skb,
						    unsigned long length)
{
	unsigned long remaining = length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;
	int frags = 0;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;

		if (unlikely(frags == omx_skb_frags))
			/* cannot add a new frag, return an error and let the caller free the skb */
			return -1;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;

		/* append the page */
		get_page(*page);
		skb_fill_page_desc(skb, frags, *page, pageoff, chunk);
		dprintk(REG, "appending %d from page\n", chunk);

		/* update the status */
		frags++;
		remaining -= chunk;

		if (pageoff + chunk == PAGE_SIZE) {
			/* next page */
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &cache->seg->pages[0]));
		} else {
			/* same page */
			pageoff += chunk;
		}
	}

	skb->len += length;
	skb->data_len += length;

	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif
	return 0;
}

static int
omx_user_region_offset_cache_vect_append_callback(struct omx_user_region_offset_cache * cache,
						  struct sk_buff * skb,
						  unsigned long length)
{
	struct omx_user_region *region = cache->region;
	unsigned long remaining = length;
	struct omx_user_region_segment *seg = cache->seg;
	unsigned long segoff = cache->segoff;
	unsigned long seglen = seg->length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;
	int frags = 0;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;

		if (unlikely(frags == omx_skb_frags))
			/* cannot add a new frag, return an error and let the caller free the skb */
			return -1;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;
		if (chunk > seglen - segoff)
			chunk = seglen - segoff;

		/* append the page */
		get_page(*page);
		skb_fill_page_desc(skb, frags, *page, pageoff, chunk);
		dprintk(REG, "appending %d from page\n", chunk);

		/* update the status */
		frags++;
		remaining -= chunk;

		if (segoff + chunk == seg->length) {
			/* next segment */
			seg++;
			segoff = 0;
			if ((char *)seg - (char *)&region->segments[0]
			    > region->nr_segments * sizeof(struct omx_user_region_segment)) {
				/* we went out of the segment array, we got to be at the end of the request */
				BUG_ON(remaining != 0);
			} else {
				seglen = seg->length;
				page = &seg->pages[0];
				pageoff = seg->first_page_offset;
				dprintk(REG, "switching offset cache to next segment #%ld\n",
					(unsigned long) (seg - &region->segments[0]));
			}
		} else if (pageoff + chunk == PAGE_SIZE) {
			/* next page in same segment */
			segoff += chunk;
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &seg->pages[0]));
		} else {
			/* same page */
			segoff += chunk;
			pageoff += chunk;
		}
	}

	skb->len += length;
	skb->data_len += length;

	cache->seg = seg;
	cache->segoff = segoff;
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif
	return 0;
}

static void
omx_user_region_offset_cache_contig_copy_callback(struct omx_user_region_offset_cache * cache,
						  void *buffer,
						  unsigned long length)
{
	unsigned long remaining = length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;
		void * kpaddr;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;

		/* append the page */
		kpaddr = kmap_atomic(*page, KM_SKB_DATA_SOFTIRQ);
		memcpy(buffer, kpaddr + pageoff, chunk);
		kunmap_atomic(kpaddr, KM_SKB_DATA_SOFTIRQ);
		dprintk(REG, "copying %d from kmapped page\n", chunk);

		/* update the status */
		remaining -= chunk;
		buffer += chunk;

		if (pageoff + chunk == PAGE_SIZE) {
			/* next page */
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &cache->seg->pages[0]));
		} else {
			/* same page */
			pageoff += chunk;
		}
	}

	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif
}

static void
omx_user_region_offset_cache_vect_copy_callback(struct omx_user_region_offset_cache * cache,
						void *buffer,
						unsigned long length)
{
	struct omx_user_region *region = cache->region;
	unsigned long remaining = length;
	struct omx_user_region_segment *seg = cache->seg;
	unsigned long segoff = cache->segoff;
	unsigned long seglen = seg->length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;
		void * kpaddr;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;
		if (chunk > seglen - segoff)
			chunk = seglen - segoff;

		/* append the page */
		kpaddr = kmap_atomic(*page, KM_SKB_DATA_SOFTIRQ);
		memcpy(buffer, kpaddr + pageoff, chunk);
		kunmap_atomic(kpaddr, KM_SKB_DATA_SOFTIRQ);
		dprintk(REG, "copying %d from kmapped page\n", chunk);

		/* update the status */
		remaining -= chunk;
		buffer += chunk;

		if (segoff + chunk == seg->length) {
			/* next segment */
			seg++;
			segoff = 0;
			if ((char *)seg - (char *)&region->segments[0]
			    > region->nr_segments * sizeof(struct omx_user_region_segment)) {
				/* we went out of the segment array, we got to be at the end of the request */
				BUG_ON(remaining != 0);
			} else {
				seglen = seg->length;
				page = &seg->pages[0];
				pageoff = seg->first_page_offset;
				dprintk(REG, "switching offset cache to next segment #%ld\n",
					(unsigned long) (seg - &region->segments[0]));
			}
		} else if (pageoff + chunk == PAGE_SIZE) {
			/* next page in same segment */
			segoff += chunk;
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &seg->pages[0]));
		} else {
			/* same page */
			segoff += chunk;
			pageoff += chunk;
		}
	}

	cache->seg = seg;
	cache->segoff = segoff;
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif
}

#ifdef CONFIG_NET_DMA
/**************************
 * DMA Copy to User-Region
 */

static int
omx_user_region_offset_cache_dma_contig_memcpy_from_buf_callback(struct omx_user_region_offset_cache *cache,
								 struct dma_chan *chan, dma_cookie_t *cookiep,
								 void *buffer,
								 unsigned long length)
{
	unsigned long remaining = length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;
		dma_cookie_t cookie;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;

		/* append the page */
		cookie = dma_async_memcpy_buf_to_pg(chan,
						    *page, pageoff,
						    buffer,
						    chunk);
		if (cookie < 0)
			goto out;
		*cookiep = cookie;

		dprintk(REG, "dma copying %d from buffer to region\n", chunk);

		/* update the status */
		remaining -= chunk;
		buffer += chunk;

		if (pageoff + chunk == PAGE_SIZE) {
			/* next page */
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &cache->seg->pages[0]));
		} else {
			/* same page */
			pageoff += chunk;
		}
	}

	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif
	return 0;

 out:
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length-remaining;
#endif
	return remaining;
}

static int
omx_user_region_offset_cache_dma_vect_memcpy_from_buf_callback(struct omx_user_region_offset_cache *cache,
							       struct dma_chan *chan, dma_cookie_t *cookiep,
							       void *buffer,
							       unsigned long length)
{
	struct omx_user_region *region = cache->region;
	unsigned long remaining = length;
	struct omx_user_region_segment *seg = cache->seg;
	unsigned long segoff = cache->segoff;
	unsigned long seglen = seg->length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;
		dma_cookie_t cookie;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;
		if (chunk > seglen - segoff)
			chunk = seglen - segoff;

		/* append the page */
		cookie = dma_async_memcpy_buf_to_pg(chan,
						    *page, pageoff,
						    buffer,
						    chunk);
		if (cookie < 0)
			goto out;
		*cookiep = cookie;

		dprintk(REG, "dma copying %d from buffer to region\n", chunk);

		/* update the status */
		remaining -= chunk;
		buffer += chunk;

		if (segoff + chunk == seg->length) {
			/* next segment */
			seg++;
			segoff = 0;
			if ((char *)seg - (char *)&region->segments[0]
			    > region->nr_segments * sizeof(struct omx_user_region_segment)) {
				/* we went out of the segment array, we got to be at the end of the request */
				BUG_ON(remaining != 0);
			} else {
				seglen = seg->length;
				page = &seg->pages[0];
				pageoff = seg->first_page_offset;
				dprintk(REG, "switching offset cache to next segment #%ld\n",
					(unsigned long) (seg - &region->segments[0]));
			}
		} else if (pageoff + chunk == PAGE_SIZE) {
			/* next page in same segment */
			segoff += chunk;
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &seg->pages[0]));
		} else {
			/* same page */
			segoff += chunk;
			pageoff += chunk;
		}
	}

	cache->seg = seg;
	cache->segoff = segoff;
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif

 out:
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length-remaining;
#endif
	return remaining;
}

static int
omx_user_region_offset_cache_dma_contig_memcpy_from_pg_callback(struct omx_user_region_offset_cache *cache,
								struct dma_chan *chan, dma_cookie_t *cookiep,
								struct page * skbpage, int skbpgoff,
								unsigned long length)
{
	unsigned long remaining = length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;
		dma_cookie_t cookie;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;

		/* append the page */
		cookie = dma_async_memcpy_pg_to_pg(chan,
						   *page, pageoff,
						   skbpage, skbpgoff,
						   chunk);
		if (cookie < 0)
			goto out;
		*cookiep = cookie;

		dprintk(REG, "dma copying %d from buffer to region\n", chunk);

		/* update the status */
		remaining -= chunk;
		skbpgoff += chunk;

		if (pageoff + chunk == PAGE_SIZE) {
			/* next page */
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &cache->seg->pages[0]));
		} else {
			/* same page */
			pageoff += chunk;
		}
	}

	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif
	return 0;

 out:
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length-remaining;
#endif
	return remaining;
}

static int
omx_user_region_offset_cache_dma_vect_memcpy_from_pg_callback(struct omx_user_region_offset_cache *cache,
							      struct dma_chan *chan, dma_cookie_t *cookiep,
							      struct page * skbpage, int skbpgoff,
							      unsigned long length)
{
	struct omx_user_region *region = cache->region;
	unsigned long remaining = length;
	struct omx_user_region_segment *seg = cache->seg;
	unsigned long segoff = cache->segoff;
	unsigned long seglen = seg->length;
	struct page ** page = cache->page;
	unsigned pageoff = cache->pageoff;

#ifdef OMX_DEBUG
	BUG_ON(cache->current_offset + length >= cache->max_offset);
#endif

	while (remaining) {
		unsigned chunk;
		dma_cookie_t cookie;

		/* compute the chunk size */
		chunk = remaining;
		if (chunk > PAGE_SIZE - pageoff)
			chunk = PAGE_SIZE - pageoff;
		if (chunk > seglen - segoff)
			chunk = seglen - segoff;

		/* append the page */
		cookie = dma_async_memcpy_pg_to_pg(chan,
						   *page, pageoff,
						   skbpage, skbpgoff,
						   chunk);
		if (cookie < 0)
			goto out;
		*cookiep = cookie;

		dprintk(REG, "dma copying %d from buffer to region\n", chunk);

		/* update the status */
		remaining -= chunk;
		skbpgoff += chunk;

		if (segoff + chunk == seg->length) {
			/* next segment */
			seg++;
			segoff = 0;
			if ((char *)seg - (char *)&region->segments[0]
			    > region->nr_segments * sizeof(struct omx_user_region_segment)) {
				/* we went out of the segment array, we got to be at the end of the request */
				BUG_ON(remaining != 0);
			} else {
				seglen = seg->length;
				page = &seg->pages[0];
				pageoff = seg->first_page_offset;
				dprintk(REG, "switching offset cache to next segment #%ld\n",
					(unsigned long) (seg - &region->segments[0]));
			}
		} else if (pageoff + chunk == PAGE_SIZE) {
			/* next page in same segment */
			segoff += chunk;
			page++;
			pageoff = 0;
			dprintk(REG, "switching offset cache to next page #%ld\n",
				(unsigned long) (page - &seg->pages[0]));
		} else {
			/* same page */
			segoff += chunk;
			pageoff += chunk;
		}
	}

	cache->seg = seg;
	cache->segoff = segoff;
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length;
#endif

 out:
	cache->page = page;
	cache->pageoff = pageoff;
#ifdef OMX_DEBUG
	cache->current_offset += length-remaining;
#endif
	return remaining;
}

#endif /* CONFIG_NET_DMA */

/*********************
 * Generic Cache Init
 */

int
omx_user_region_offset_cache_init(struct omx_user_region *region,
				  struct omx_user_region_offset_cache *cache,
				  unsigned long offset, unsigned long length)
{
	struct omx_user_region_segment *seg;
	unsigned long segoff;

	if (unlikely(!region->nr_segments || offset + length > region->total_length))
		return -1;

	cache->region = region;

	if (unlikely(region->nr_segments > 1)) {
		unsigned long tmp;

		/* vectorial callbacks */
		cache->append_pages_to_skb = omx_user_region_offset_cache_vect_append_callback;
		cache->copy_pages_to_buf = omx_user_region_offset_cache_vect_copy_callback;
#ifdef CONFIG_NET_DMA
		cache->dma_memcpy_from_pg = omx_user_region_offset_cache_dma_vect_memcpy_from_pg_callback;
		cache->dma_memcpy_from_buf = omx_user_region_offset_cache_dma_vect_memcpy_from_buf_callback;
#endif

		/* find the segment */
		for(tmp=0, seg = &region->segments[0];
		    tmp + seg->length <= offset;
		    tmp += seg->length, seg++);

		/* find the segment offset */
		segoff = offset - tmp;

	} else {
		/* vectorial callbacks */
		cache->append_pages_to_skb = omx_user_region_offset_cache_contig_append_callback;
		cache->copy_pages_to_buf = omx_user_region_offset_cache_contig_copy_callback;
#ifdef CONFIG_NET_DMA
		cache->dma_memcpy_from_pg = omx_user_region_offset_cache_dma_contig_memcpy_from_pg_callback;
		cache->dma_memcpy_from_buf = omx_user_region_offset_cache_dma_contig_memcpy_from_buf_callback;
#endif

		/* use the first segment */
		seg = &region->segments[0];
		segoff = offset;
	}

	/* setup the segment and offset */
	cache->seg = seg;
	cache->segoff = segoff;

	/* find the page and offset */
	cache->page = &seg->pages[(segoff + seg->first_page_offset) >> PAGE_SHIFT];
	cache->pageoff = (segoff + seg->first_page_offset) & (~PAGE_MASK);

	dprintk(REG, "initialized region offset cache to seg #%ld offset %ld page #%ld offset %d\n",
		(unsigned long) (seg - &region->segments[0]), segoff,
		(unsigned long) (cache->page - &seg->pages[0]), cache->pageoff);

#ifdef OMX_DEBUG
	cache->current_offset = offset;
	cache->max_offset = offset + length;
#endif

	return 0;
}

/************************************
 * Filling region pages with receive
 */

static INLINE void
omx__user_region_segment_fill_pages(struct omx_user_region_segment * segment,
				    unsigned long segment_offset,
				    struct sk_buff * skb,
				    unsigned long skb_offset,
				    unsigned long length)
{
	unsigned long copied = 0;
	unsigned long remaining = length;
	unsigned long first_page = (segment_offset+segment->first_page_offset)>>PAGE_SHIFT;
	unsigned long page_offset = (segment_offset+segment->first_page_offset) & (PAGE_SIZE-1);
	unsigned long i;

	for(i=first_page; ; i++) {
		void *kvaddr;
		int err;

		/* compute chunk to take in this page */
		unsigned long chunk = PAGE_SIZE-page_offset;
		if (unlikely(chunk > remaining))
			chunk = remaining;

		/* fill the page */
		kvaddr = kmap_atomic(segment->pages[i], KM_USER0);
		err = skb_copy_bits(skb, skb_offset, kvaddr+page_offset, chunk);
		kunmap_atomic(kvaddr, KM_USER0);
		dprintk(REG,
			"filling page #%ld offset %ld from skb offset %ld with length %ld\n",
			i, page_offset, skb_offset, chunk);

		/* update counters */
		copied += chunk;
		skb_offset += chunk;
		remaining -= chunk;
		if (likely(!remaining))
			break;
		page_offset = 0;
	}

	BUG_ON(copied != length);
}

int
omx_user_region_fill_pages(struct omx_user_region * region,
			   unsigned long region_offset,
			   struct sk_buff * skb,
			   unsigned long length)
{
	unsigned long segment_offset = region_offset;
	unsigned long skb_offset = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	unsigned long copied = 0;
	unsigned long remaining = length;
	int iseg;

	if (region_offset+length > region->total_length)
		return -EINVAL;

	for(iseg=0; iseg<region->nr_segments; iseg++) {
		struct omx_user_region_segment * segment = &region->segments[iseg];
		dprintk(REG,
			"looking at segment #%d length %ld for offset %ld length %ld\n",
			iseg, (unsigned long) segment->length, segment_offset, remaining);

		/* skip segment if offset is beyond it */
		if (unlikely(segment_offset >= segment->length)) {
			segment_offset -= segment->length;
			continue;
		}

		if (unlikely(segment_offset + remaining > segment->length)) {
			/* fill the end of this segment and jump to the next one */
			unsigned long chunk = segment->length - segment_offset;
			dprintk(REG,
				"filling pages from segment #%d offset %ld length %ld\n",
				iseg, segment_offset, chunk);
			omx__user_region_segment_fill_pages(segment, segment_offset,
							    skb, skb_offset,
							    chunk);
			copied += chunk;
			skb_offset += chunk;
			remaining -= chunk;
			segment_offset = 0;
			continue;

		} else {
			/* the whole data is in this segment */
			dprintk(REG,
				"last filling pages from segment #%d offset %ld length %ld\n",
				iseg, segment_offset, remaining);
			omx__user_region_segment_fill_pages(segment, segment_offset,
							    skb, skb_offset,
							    remaining);
			copied += remaining;
			remaining = 0;
			break;
		}
	}

	BUG_ON(copied != length);
	BUG_ON(remaining != 0);
	return 0;
}

/******************************
 * Shared Copy between Regions
 */

/*
 * Copy between regions with the destination in the current process user-space
 * (no need to be pinned then)
 */
static INLINE int
omx_memcpy_between_user_regions_to_current(struct omx_user_region * src_region, unsigned long src_offset,
					   struct omx_user_region * dst_region, unsigned long dst_offset,
					   unsigned long length)
{
	unsigned long remaining = length;
	unsigned long tmp;
	struct omx_user_region_segment *sseg, *dseg; /* current segment */
	unsigned long soff; /* current offset in region */
	unsigned long sseglen, dseglen; /* length of current segment */
	unsigned long ssegoff, dsegoff; /* current offset in current segment */
	struct page **spage; /* current page */
	unsigned int spageoff; /* current offset in current page */
	void *spageaddr; /* current page mapping */
	void __user *dvaddr; /* current user-space virtual address */
	unsigned long spinlen; /* currently pinned length in region */
	int ret;

	dprintk(REG, "shared region copy of %ld bytes from region #%ld len %ld starting at %ld into region #%ld len %ld starting at %ld\n",
		length,
		(unsigned long) src_region->id, src_region->total_length, src_offset,
		(unsigned long) dst_region->id, dst_region->total_length, dst_offset);

	/* initialize the src state */
	for(tmp=0,sseg=&src_region->segments[0];; sseg++) {
		sseglen = sseg->length;
		if (tmp + sseglen > src_offset)
			break;
		tmp += sseglen;
	}
	soff = src_offset;
	ssegoff = src_offset - tmp;
	spage = &sseg->pages[(ssegoff + sseg->first_page_offset) >> PAGE_SHIFT];
	spageoff = (ssegoff + sseg->first_page_offset) & (~PAGE_MASK);
	spinlen = 0;

	/* initialize the dst state */
	for(tmp=0,dseg=&dst_region->segments[0];; dseg++) {
		dseglen = dseg->length;
		if (tmp + dseglen > dst_offset)
			break;
		tmp += dseglen;
	}
	dsegoff = dst_offset - tmp;
	dvaddr = (void __user *) dseg->aligned_vaddr + dseg->first_page_offset + dsegoff;

	while (1) {
		/* compute the chunk size */
		unsigned chunk = remaining;
		if (chunk > PAGE_SIZE - spageoff)
			chunk = PAGE_SIZE - spageoff;
		if (chunk > sseglen - ssegoff)
			chunk = sseglen - ssegoff;
		if (chunk > dseglen - dsegoff)
			chunk = dseglen - dsegoff;

		if (omx_region_demand_pin && spinlen < soff + chunk) {
			spinlen = soff + chunk;
			ret = omx_user_region_parallel_pin_wait(src_region, &spinlen);
			if (ret < 0)
				return ret;
		}
		/* *spage is valid now */

		dprintk(REG, "shared region copy of %d bytes from seg=%ld:page=%ld(%p):off=%d to seg=%ld:off=%ld\n",
			chunk,
			(unsigned long) (sseg-&src_region->segments[0]), (unsigned long) (spage-&sseg->pages[0]), *spage, spageoff,
			(unsigned long) (dseg-&dst_region->segments[0]), dsegoff);

		spageaddr = kmap(*spage);
		ret = copy_to_user(dvaddr, spageaddr + spageoff, chunk);
		kunmap(*spage);
		if (ret)
			return -EFAULT;

		soff += chunk;
		remaining -= chunk;
		if (!remaining)
			break;

		/* update the source */
		if (ssegoff + chunk == sseglen) {
			/* next segment */
			sseg++;
			sseglen = sseg->length;
			dprintk(REG, "shared region copy switching to source seg %ld len %ld, %ld remaining\n",
				(unsigned long) (sseg-&src_region->segments[0]), sseglen, remaining);
			ssegoff = 0;
			spage = &sseg->pages[0];
			spageoff = sseg->first_page_offset;
		} else if (spageoff + chunk == PAGE_SIZE) {
			/* next page */
			ssegoff += chunk;
			spage++;
			spageoff = 0;
		} else {
			/* same page */
			ssegoff += chunk;
			spageoff += chunk;
		}

		/* update the destination */
		if (dsegoff + chunk == dseglen) {
			/* next segment */
			dseg++;
			dseglen = dseg->length;
			dprintk(REG, "shared region copy switching to dest seg %ld len %ld, %ld remaining\n",
				(unsigned long) (dseg-&dst_region->segments[0]), dseglen, remaining);
			dsegoff = 0;
			dvaddr = (void __user *) dseg->aligned_vaddr + dseg->first_page_offset;
		} else {
			/* same page */
			dsegoff += chunk;
			dvaddr += chunk;
		}
	}

	return 0;
}

#ifdef CONFIG_NET_DMA
static INLINE int
omx_dma_copy_between_user_regions(struct omx_user_region * src_region, unsigned long src_offset,
				  struct omx_user_region * dst_region, unsigned long dst_offset,
				  unsigned long length)
{
	unsigned long remaining = length;
	unsigned long tmp;
	struct omx_user_region_segment *sseg, *dseg; /* current segment */
	unsigned long soff, doff; /* current offset in region */
	unsigned long sseglen, dseglen; /* length of current segment */
	unsigned long ssegoff, dsegoff; /* current offset in current segment */
	struct page **spage, **dpage; /* current page */
	unsigned int spageoff, dpageoff; /* current offset in current page */
	unsigned long spinlen, dpinlen; /* currently pinned length in region */
	struct omx_user_region_pin_state dpinstate;
	struct dma_chan *dma_chan = NULL;
	dma_cookie_t dma_last_cookie = -1;
	int ret = 0;

	dma_chan = get_softnet_dma();
	if (!dma_chan)
		goto fallback;

	if (omx_region_demand_pin)
		omx_user_region_demand_pin_init(&dpinstate, dst_region);

	dprintk(REG, "shared region copy of %ld bytes from region #%ld len %ld starting at %ld into region #%ld len %ld starting at %ld\n",
		length,
		(unsigned long) src_region->id, src_region->total_length, src_offset,
		(unsigned long) dst_region->id, dst_region->total_length, dst_offset);

	/* initialize the src state */
	for(tmp=0,sseg=&src_region->segments[0];; sseg++) {
		sseglen = sseg->length;
		if (tmp + sseglen > src_offset)
			break;
		tmp += sseglen;
	}
	soff = src_offset;
	ssegoff = src_offset - tmp;
	spage = &sseg->pages[(ssegoff + sseg->first_page_offset) >> PAGE_SHIFT];
	spageoff = (ssegoff + sseg->first_page_offset) & (~PAGE_MASK);
	spinlen = 0;

	/* initialize the dst state */
	for(tmp=0,dseg=&dst_region->segments[0];; dseg++) {
		dseglen = dseg->length;
		if (tmp + dseglen > dst_offset)
			break;
		tmp += dseglen;
	}
	doff = dst_offset;
	dsegoff = dst_offset - tmp;
	dpage = &dseg->pages[(dsegoff + dseg->first_page_offset) >> PAGE_SHIFT];
	dpageoff = (dsegoff + dseg->first_page_offset) & (~PAGE_MASK);
	dpinlen = 0;

	while (1) {
		dma_cookie_t cookie;
		/* compute the chunk size */
		unsigned chunk = remaining;
		if (chunk > PAGE_SIZE - spageoff)
			chunk = PAGE_SIZE - spageoff;
		if (chunk > sseglen - ssegoff)
			chunk = sseglen - ssegoff;
		if (chunk > PAGE_SIZE - dpageoff)
			chunk = PAGE_SIZE - dpageoff;
		if (chunk > dseglen - dsegoff)
			chunk = dseglen - dsegoff;

		if (omx_region_demand_pin) {
			if (spinlen < soff + chunk) {
				spinlen = soff + chunk;
				ret = omx_user_region_parallel_pin_wait(src_region, &spinlen);
				if (ret < 0) {
					/* failed to pin, no need to fallback to memcpy */
					remaining = 0;
					break;
				}
			}

			if (dpinlen < doff + chunk) {
				dpinlen = doff + chunk;
				ret = omx_user_region_demand_pin_continue(&dpinstate, &dpinlen);
				if (ret < 0)
				if (ret < 0) {
					/* failed to pin, no need to fallback to memcpy */
					remaining = 0;
					break;
				}
			}
		}
		/* *spage and *dpage are valid now */

		dprintk(REG, "shared region copy of %d bytes from seg=%ld:page=%ld(%p):off=%d to seg=%ld:page=%ld(%p):off=%d\n",
			chunk,
			(unsigned long) (sseg-&src_region->segments[0]), (unsigned long) (spage-&sseg->pages[0]), *spage, spageoff,
			(unsigned long) (dseg-&dst_region->segments[0]), (unsigned long) (dpage-&dseg->pages[0]), *dpage, dpageoff);

		cookie = dma_async_memcpy_pg_to_pg(dma_chan, *dpage, dpageoff, *spage, spageoff, chunk);
		if (cookie < 0)
			/* fallback to memcpy */
			break;
		dma_last_cookie = cookie;

		soff += chunk;
		doff += chunk;
		remaining -= chunk;
		if (!remaining)
			break;

		/* update the source */
		if (ssegoff + chunk == sseglen) {
			/* next segment */
			sseg++;
			sseglen = sseg->length;
			dprintk(REG, "shared region copy switching to source seg %ld len %ld, %ld remaining\n",
				(unsigned long) (sseg-&src_region->segments[0]), sseglen, remaining);
			ssegoff = 0;
			spage = &sseg->pages[0];
			spageoff = sseg->first_page_offset;
		} else if (spageoff + chunk == PAGE_SIZE) {
			/* next page */
			ssegoff += chunk;
			spage++;
			spageoff = 0;
		} else {
			/* same page */
			ssegoff += chunk;
			spageoff += chunk;
		}

		/* update the destination */
		if (dsegoff + chunk == dseglen) {
			/* next segment */
			dseg++;
			dseglen = dseg->length;
			dprintk(REG, "shared region copy switching to dest seg %ld len %ld, %ld remaining\n",
				(unsigned long) (dseg-&dst_region->segments[0]), dseglen, remaining);
			dsegoff = 0;
			dpage = &dseg->pages[0];
			dpageoff = dseg->first_page_offset;
		} else if (dpageoff + chunk == PAGE_SIZE) {
			/* next page */
			dsegoff += chunk;
			dpage++;
			dpageoff = 0;
		} else {
			/* same page */
			dsegoff += chunk;
			dpageoff += chunk;
		}
	}

	if (omx_region_demand_pin) {
		omx_user_region_demand_pin_finish(&dpinstate);
		/* ignore the return value, only the copy success matters */
	}
	/* either the region is entirely pinned, or not at all,
	 * it's safe to fallback to memcpy if needed */

 fallback:
	if (remaining) {
		ret = omx_memcpy_between_user_regions_to_current(src_region, src_offset + (length - remaining),
							   dst_region, dst_offset + (length - remaining),
							   remaining);
		omx_counter_inc(omx_shared_fake_iface, SHARED_DMA_PARTIAL_LARGE);
	} else {
		omx_counter_inc(omx_shared_fake_iface, SHARED_DMA_LARGE);
	}

	/* wait for dma completion at the end, to overlap a bit with everything else */
	if (dma_chan) {
		if (dma_last_cookie > 0) {
			dma_async_memcpy_issue_pending(dma_chan);
			while (dma_async_memcpy_complete(dma_chan, dma_last_cookie, NULL, NULL) == DMA_IN_PROGRESS);
		}
		dma_chan_put(dma_chan);
	}

	return ret;
}
#endif /* CONFIG_NET_DMA */

int
omx_copy_between_user_regions(struct omx_user_region * src_region, unsigned long src_offset,
			      struct omx_user_region * dst_region, unsigned long dst_offset,
			      unsigned long length)
{
	if (unlikely(!length))
		return 0;

	if (src_offset + length > src_region->total_length
	    || dst_offset + length > dst_region->total_length)
		return -EINVAL;

#ifdef CONFIG_NET_DMA
	if (omx_dmaengine && length >= omx_dma_sync_min)
		return omx_dma_copy_between_user_regions(src_region, src_offset, dst_region, dst_offset, length);
	else
#endif /* CONFIG_NET_DMA */
		return omx_memcpy_between_user_regions_to_current(src_region, src_offset, dst_region, dst_offset, length);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
