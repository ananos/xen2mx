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
#include "omx_region.h"

#ifdef OMX_MX_WIRE_COMPAT
#if OMX_USER_REGION_MAX > 256
#error Cannot store region id > 255 in 8bit id on the wire
#endif
#endif

/********************************************
 * Low-level segments and pages registration
 */

static int
omx_user_region_register_segment(struct omx_cmd_region_segment * useg,
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

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (unlikely(!pages)) {
		printk(KERN_ERR "Open-MX: Failed to allocate user region segment page array\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = get_user_pages(current, current->mm, aligned_vaddr, nr_pages, 1, 0, pages, NULL);
	if (unlikely(ret < 0)) {
		printk(KERN_ERR "Open-MX: get_user_pages failed (error %d)\n", ret);
		goto out_with_pages;
	}
	BUG_ON(ret != nr_pages);

	segment->first_page_offset = offset;
	segment->length = useglen;
	segment->nr_pages = nr_pages;
	segment->pages = pages;

	return 0;

 out_with_pages:
	kfree(pages);
 out:
	return ret;
}

static void
omx_user_region_deregister_segment(struct omx_user_region_segment * segment)
{
	unsigned long i;
	for(i=0; i<segment->nr_pages; i++)
		put_page(segment->pages[i]);
	kfree(segment->pages);
}

static void
omx_user_region_deregister_segments(struct omx_user_region * region)
{
	int i;
	for(i=0; i<region->nr_segments; i++)
		omx_user_region_deregister_segment(&region->segments[i]);
}

/**********************
 * Region registration
 */

int
omx_ioctl_user_region_register(struct omx_endpoint * endpoint,
			       void __user * uparam)
{
	struct omx_cmd_register_region cmd;
	struct omx_user_region * region;
	struct omx_user_region_segment *seg;
	struct omx_cmd_region_segment * usegs;
	int ret, i;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read register region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.id >= OMX_USER_REGION_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot register invalid region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}

	/* get the list of segments */
	usegs = kmalloc(sizeof(struct omx_cmd_region_segment) * cmd.nr_segments,
			GFP_KERNEL);
	if (unlikely(!usegs)) {
		printk(KERN_ERR "Open-MX: Failed to allocate segments for user region\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = copy_from_user(usegs, (void __user *)(unsigned long) cmd.segments,
			     sizeof(struct omx_cmd_region_segment) * cmd.nr_segments);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read register region cmd\n");
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

	/* keep nr_segments exact so that we may call omx__deregister_user_region safely */
	region->nr_segments = 0;

	down_write(&current->mm->mmap_sem);

	for(i=0, seg = &region->segments[0]; i<cmd.nr_segments; i++) {
		if (!usegs[i].len)
			continue;
		ret = omx_user_region_register_segment(&usegs[i], seg);
		if (unlikely(ret < 0)) {
			up_write(&current->mm->mmap_sem);
			goto out_with_region;
		}
		region->nr_segments++;
		region->total_length += seg->length;
		seg++;
	}

	up_write(&current->mm->mmap_sem);

	spin_lock(&endpoint->user_regions_lock);

	if (unlikely(endpoint->user_regions[cmd.id] != NULL)) {
		printk(KERN_ERR "Open-MX: Cannot register busy region %d\n", cmd.id);
		ret = -EBUSY;
		spin_unlock(&endpoint->user_regions_lock);
		goto out_with_region;
	}

	region->id = cmd.id;
	rcu_assign_pointer(endpoint->user_regions[cmd.id], region);

	spin_unlock(&endpoint->user_regions_lock);

	kfree(usegs);
	return 0;

 out_with_region:
	omx_user_region_deregister_segments(region);
	kfree(region);
 out_with_usegs:
	kfree(usegs);
 out:
	return ret;
}

/************************
 * Region deregistration
 */

/* Called when the last reference on the region is released */
void
__omx_user_region_last_release(struct kref * kref)
{
	struct omx_user_region * region = container_of(kref, struct omx_user_region, refcount);

	dprintk(KREF, "releasing the last reference on region %p\n",
		region);

	omx_user_region_deregister_segments(region);
	kfree(region);
}

static void
__omx_user_region_rcu_release_callback(struct rcu_head *rcu_head)
{
	struct omx_user_region * region = container_of(rcu_head, struct omx_user_region, rcu_head);
	kref_put(&region->refcount, __omx_user_region_last_release);
}

int
omx_ioctl_user_region_deregister(struct omx_endpoint * endpoint,
			         void __user * uparam)
{
	struct omx_cmd_deregister_region cmd;
	struct omx_user_region * region;
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read deregister region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	ret = -EINVAL;
	if (unlikely(cmd.id >= OMX_USER_REGION_MAX)) {
		printk(KERN_ERR "Open-MX: Cannot deregister invalid region %d\n", cmd.id);
		goto out;
	}

	spin_lock(&endpoint->user_regions_lock);

	region = endpoint->user_regions[cmd.id];
	if (unlikely(!region)) {
		printk(KERN_ERR "Open-MX: Cannot register unexisting region %d\n", cmd.id);
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

		dprintk(REG, "forcing deregister of window %d on endpoint %d board %d\n",
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

static inline void
omx__user_region_segment_append_pages(struct omx_user_region_segment * segment,
				      unsigned long segment_offset,
				      struct sk_buff * skb,
				      unsigned long length,
				      int *fragp)
{
	unsigned long queued = 0;
	unsigned long remaining = length;
	unsigned long first_page = (segment_offset+segment->first_page_offset)>>PAGE_SHIFT;
	unsigned long page_offset = (segment_offset+segment->first_page_offset) & (PAGE_SIZE-1);
	unsigned long i;

	for(i=first_page; ; i++) {
		/* compute chunk to take in this page */
		unsigned long chunk = PAGE_SIZE-page_offset;
		if (unlikely(chunk > remaining))
			chunk = remaining;

		/* append the page */
		get_page(segment->pages[i]);
		skb_fill_page_desc(skb, *fragp, segment->pages[i], page_offset, chunk);
		skb->len += chunk;
		skb->data_len += chunk;
		dprintk(REG,
			"appending page #%ld offset %ld to skb frag #%d with length %ld\n",
			i, page_offset, *fragp, chunk);

		/* update skb frags counter */
		(*fragp)++;
		BUG_ON(*fragp == MAX_SKB_FRAGS-1); /* FIXME: detect earlier and linearize? */

		/* update counters */
		queued += chunk;
		remaining -= chunk;
		if (likely(!remaining))
			break;
		page_offset = 0;
	}

	BUG_ON(queued != length);
}

int
omx_user_region_append_pages(struct omx_user_region * region,
			     unsigned long region_offset,
			     struct sk_buff * skb,
			     unsigned long length)
{
	unsigned long segment_offset = region_offset;
	unsigned long queued = 0;
	unsigned long remaining = length;
	int iseg;
	int frag = 0;

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
			/* take the end of this segment and jump to the next one */
			unsigned long chunk = segment->length - segment_offset;
			dprintk(REG,
				"appending pages from segment #%d offset %ld length %ld\n",
				iseg, segment_offset, chunk);
			omx__user_region_segment_append_pages(segment, segment_offset,
							      skb,
							      chunk,
							      &frag);
			queued += chunk;
			remaining -= chunk;
			segment_offset = 0;
			continue;

		} else {
			/* the whole data is in this segment */
			dprintk(REG,
				"last appending pages from segment #%d offset %ld length %ld\n",
				iseg, segment_offset, remaining);
			omx__user_region_segment_append_pages(segment, segment_offset,
							      skb,
							      remaining,
							      &frag);
			queued += remaining;
			remaining = 0;
			break;
		}
	}

	BUG_ON(queued != length);
	BUG_ON(remaining != 0);
	return 0;
}

/************************************
 * Filling region pages with receive
 */

static inline void
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

/************************************
 * Filling region pages with receive
 */

int
omx_copy_between_user_regions(struct omx_user_region * src_region, unsigned long src_offset,
			      struct omx_user_region * dst_region, unsigned long dst_offset,
			      unsigned long length)
{
	/* TODO */

	return 0;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
