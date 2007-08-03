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
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include "omx_hal.h"
#include "omx_io.h"
#include "omx_common.h"

/* FIXME: likely/unlikely */

/***************************
 * User region registration
 */

void
omx_endpoint_user_regions_init(struct omx_endpoint * endpoint)
{
	memset(endpoint->user_regions, 0, sizeof(endpoint->user_regions));
	spin_lock_init(&endpoint->user_regions_lock);
}

static int
omx_user_region_register_segment(struct omx_cmd_region_segment * useg,
				 struct omx_user_region_segment * segment)
{
	struct page ** pages;
	unsigned offset;
	unsigned long aligned_vaddr;
	unsigned long aligned_len;
	unsigned long nr_pages;
	int ret;

	offset = useg->vaddr & (~PAGE_MASK);
	aligned_vaddr = useg->vaddr & PAGE_MASK;
	aligned_len = PAGE_ALIGN(offset + useg->len);
	nr_pages = aligned_len >> PAGE_SHIFT;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		printk(KERN_ERR "Open-MX: Failed to allocate user region segment page array\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = get_user_pages(current, current->mm, aligned_vaddr, nr_pages, 1, 0, pages, NULL);
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: get_user_pages failed (error %d)\n", ret);
		goto out_with_pages;
	}
	BUG_ON(ret != nr_pages);

	segment->offset = offset;
	segment->length = useg->len;
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
omx__user_region_deregister(struct omx_user_region * region)
{
	int i;
	for(i=0; i<region->nr_segments; i++)
		omx_user_region_deregister_segment(&region->segments[i]);
	kfree(region);
}

int
omx_user_region_register(struct omx_endpoint * endpoint,
			 void __user * uparam)
{
	struct omx_cmd_register_region cmd;
	struct omx_user_region * region;
	struct omx_cmd_region_segment * usegs;
	int ret, i;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "Open-MX: Failed to read register region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd.id >= OMX_USER_REGION_MAX) {
		printk(KERN_ERR "Open-MX: Cannot register invalid region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}

	/* get the list of segments */
	usegs = kmalloc(sizeof(struct omx_cmd_region_segment) * cmd.nr_segments,
			GFP_KERNEL);
	if (!usegs) {
		printk(KERN_ERR "Open-MX: Failed to allocate segments for user region\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = copy_from_user(usegs, (void __user *)(unsigned long) cmd.segments,
			     sizeof(struct omx_cmd_region_segment) * cmd.nr_segments);
	if (ret) {
		printk(KERN_ERR "Open-MX: Failed to read register region cmd\n");
		ret = -EFAULT;
		goto out_with_usegs;
	}

	/* allocate the region */
	region = kzalloc(sizeof(struct omx_user_region)
			 + cmd.nr_segments * sizeof(struct omx_user_region_segment),
			 GFP_KERNEL);
	if (!region) {
		printk(KERN_ERR "Open-MX: failed to allocate user region\n");
		ret = -ENOMEM;
		goto out_with_usegs;
	}

	spin_lock_init(&region->lock);
	region->status = OMX_USER_REGION_STATUS_OK;
	atomic_set(&region->refcount, 0);
	init_waitqueue_head(&region->noref_queue);

	/* keep nr_segments exact so that we may call omx__deregister_user_region safely */
	region->nr_segments = 0;

	down_write(&current->mm->mmap_sem);

	for(i=0; i<cmd.nr_segments; i++) {
		ret = omx_user_region_register_segment(&usegs[i], &region->segments[i]);
		if (ret < 0) {
			up_write(&current->mm->mmap_sem);
			goto out_with_region;
		}
		region->nr_segments++;
	}

	up_write(&current->mm->mmap_sem);

	spin_lock(&endpoint->user_regions_lock);

	if (endpoint->user_regions[cmd.id]) {
		printk(KERN_ERR "Open-MX: Cannot register busy region %d\n", cmd.id);
		ret = -EBUSY;
		spin_unlock(&endpoint->user_regions_lock);
		goto out_with_region;
	}
	endpoint->user_regions[cmd.id] = region;

	spin_unlock(&endpoint->user_regions_lock);

	kfree(usegs);
	return 0;

 out_with_region:
	omx__user_region_deregister(region);
 out_with_usegs:
	kfree(usegs);
 out:
	return ret;
}

struct omx_user_region *
omx_user_region_acquire(struct omx_endpoint * endpoint,
			uint32_t rdma_id)
{
	struct omx_user_region * region;

	spin_lock(&endpoint->user_regions_lock);
	if (rdma_id >= OMX_USER_REGION_MAX)
		goto out_with_endpoint_lock;
	region = endpoint->user_regions[rdma_id];
	if (!region)
		goto out_with_endpoint_lock;

	spin_lock(&region->lock);
	if (region->status != OMX_USER_REGION_STATUS_OK)
		goto out_with_region_lock;

	atomic_inc(&region->refcount);
	spin_unlock(&region->lock);
	spin_unlock(&endpoint->user_regions_lock);

	return region;

 out_with_region_lock:
	spin_unlock(&region->lock);
 out_with_endpoint_lock:
	spin_unlock(&endpoint->user_regions_lock);
	return NULL;
}

void
omx_user_region_release(struct omx_user_region * region)
{
	/* decrement refcount and wake up the closer */
	if (unlikely(atomic_dec_and_test(&region->refcount)))
		wake_up(&region->noref_queue);
}

int
omx_user_region_deregister(struct omx_endpoint * endpoint,
			   void __user * uparam)
{
	struct omx_cmd_deregister_region cmd;
	struct omx_user_region * region;
	DECLARE_WAITQUEUE(wq, current);
	int ret;

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (ret) {
		printk(KERN_ERR "Open-MX: Failed to read deregister region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	ret = -EINVAL;
	if (cmd.id >= OMX_USER_REGION_MAX) {
		printk(KERN_ERR "Open-MX: Cannot deregister invalid region %d\n", cmd.id);
		goto out;
	}

	spin_lock(&endpoint->user_regions_lock);

	region = endpoint->user_regions[cmd.id];
	if (!region) {
		printk(KERN_ERR "Open-MX: Cannot register unexisting region %d\n", cmd.id);
		goto out_with_endpoint_lock;
	}

	spin_lock(&region->lock);
	if (region->status != OMX_USER_REGION_STATUS_OK)
		goto out_with_region_lock;

	/* mark it as closing so that nobody may use it again */
	region->status = OMX_USER_REGION_STATUS_CLOSING;

	spin_unlock(&region->lock);
	spin_unlock(&endpoint->user_regions_lock);

	/* wait until refcount is 0 so that other users are gone */
	add_wait_queue(&region->noref_queue, &wq);
	for(;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!atomic_read(&region->refcount))
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&endpoint->noref_queue, &wq);

	/* release the region now that nobody uses it */
	spin_lock(&endpoint->user_regions_lock);
	omx__user_region_deregister(region);
	endpoint->user_regions[cmd.id] = NULL;
	spin_unlock(&endpoint->user_regions_lock);

	return 0;

 out_with_region_lock:
	spin_unlock(&region->lock);
 out_with_endpoint_lock:
	spin_unlock(&endpoint->user_regions_lock);
 out:
	return ret;
}

void
omx_endpoint_user_regions_exit(struct omx_endpoint * endpoint)
{
	struct omx_user_region * region;
	int i;

	for(i=0; i<OMX_USER_REGION_MAX; i++) {
		region = endpoint->user_regions[i];
		if (!region)
			continue;

		printk(KERN_INFO "Open-MX: Forcing deregister of window %d on endpoint %d board %d\n",
		       i, endpoint->endpoint_index, endpoint->iface->index);
		omx__user_region_deregister(region);
		endpoint->user_regions[i] = NULL;
	}
}

/*********************************
 * Appending region pages to send
 */

static inline int
omx__user_region_segment_append_pages(struct omx_user_region_segment * segment,
				      int segment_offset,
				      struct sk_buff * skb,
				      int length,
				      int *fragp)
{
	int queued = 0;
	int remaining = length;
	int first_page = (segment_offset+segment->offset)>>PAGE_SHIFT;
	int page_offset = (segment_offset+segment->offset) & (PAGE_SIZE-1);
	int i;

	for(i=first_page; ; i++) {
		/* compute chunk to take in this page */
		int chunk = PAGE_SIZE-page_offset;
		if (chunk > remaining)
			chunk = remaining;

		/* append the page */
		get_page(segment->pages[i]);
		skb_fill_page_desc(skb, *fragp, segment->pages[i], page_offset, chunk);
		skb->len += chunk;
		skb->data_len += chunk;
		printk("appending page #%d offset %d to skb frag #%d with length %d\n",
		       i, page_offset, *fragp, chunk);

		/* update skb frags counter */
		(*fragp)++;
		BUG_ON(*fragp == MAX_SKB_FRAGS-1); /* FIXME: detect earlier and linearize? */

		/* update counters */
		queued += chunk;
		remaining -= chunk;
		if (!remaining)
			break;
		page_offset = 0;
	}

	BUG_ON(queued != length);
	return 0;
}

int
omx_user_region_append_pages(struct omx_user_region * region,
			     int region_offset,
			     struct sk_buff * skb,
			     int length)
{
	int segment_offset = region_offset;
	int queued = 0;
	int remaining = length;
	int iseg;
	int frag = 0;

	for(iseg=0; iseg<region->nr_segments; iseg++) {
		struct omx_user_region_segment * segment = &region->segments[iseg];
		printk("looking at segment #%d length %ld for offset %d length %d\n",
		       iseg, (unsigned long) segment->length, segment_offset, remaining);

		/* skip segment if offset is beyond it */
		if (segment_offset >= segment->length) {
			segment_offset -= segment->length;
			continue;
		}

		if (segment_offset + remaining > segment->length) {
			/* take the end of this segment and jump to the next one */
			int chunk = segment->length - segment_offset;
			printk("appending pages from segment #%d offset %d length %d\n",
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
			printk("last appending pages from segment #%d offset %d length %d\n",
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

static inline int
omx__user_region_segment_fill_pages(struct omx_user_region_segment * segment,
				    int segment_offset,
				    struct sk_buff * skb,
				    int skb_offset,
				    int length)
{
	int copied = 0;
	int remaining = length;
	int first_page = (segment_offset+segment->offset)>>PAGE_SHIFT;
	int page_offset = (segment_offset+segment->offset) & (PAGE_SIZE-1);
	int i;

	for(i=first_page; ; i++) {
		void *kvaddr;
		int err;

		/* compute chunk to take in this page */
		int chunk = PAGE_SIZE-page_offset;
		if (chunk > remaining)
			chunk = remaining;

		/* fill the page */
		kvaddr = kmap_atomic(segment->pages[i], KM_USER0);
		err = skb_copy_bits(skb, skb_offset, kvaddr+page_offset, chunk);
		kunmap_atomic(kvaddr, KM_USER0);
		printk("filling page #%d offset %d from skb offset %d with length %d\n",
		       i, page_offset, skb_offset, chunk);

		/* update counters */
		copied += chunk;
		skb_offset += chunk;
		remaining -= chunk;
		if (!remaining)
			break;
		page_offset = 0;
	}

	BUG_ON(copied != length);
	return 0;
}

int
omx_user_region_fill_pages(struct omx_user_region * region,
			   int region_offset,
			   struct sk_buff * skb,
			   int length)
{
	int segment_offset = region_offset;
	int skb_offset = sizeof(struct omx_hdr);
	int copied = 0;
	int remaining = length;
	int iseg;

	for(iseg=0; iseg<region->nr_segments; iseg++) {
		struct omx_user_region_segment * segment = &region->segments[iseg];
		printk("looking at segment #%d length %ld for offset %d length %d\n",
		       iseg, (unsigned long) segment->length, segment_offset, remaining);

		/* skip segment if offset is beyond it */
		if (segment_offset >= segment->length) {
			segment_offset -= segment->length;
			continue;
		}

		if (segment_offset + remaining > segment->length) {
			/* fill the end of this segment and jump to the next one */
			int chunk = segment->length - segment_offset;
			printk("filling pages from segment #%d offset %d length %d\n",
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
			printk("last filling pages from segment #%d offset %d length %d\n",
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

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
