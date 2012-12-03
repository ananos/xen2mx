/*
 * Xen2MX
 * Copyright © Anastassios Nanos 2012
 * (see AUTHORS file)
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
#include <linux/utsname.h>
#include <linux/if_arp.h>
#include <linux/rcupdate.h>
#include <linux/ethtool.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#ifdef OMX_HAVE_MUTEX
#include <linux/mutex.h>
#endif

#include <stdarg.h>
#include <xen/interface/io/xenbus.h>
#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/page.h>

//#define TIMERS_ENABLED
#include "omx_xen_timers.h"

#include "omx_common.h"
#include "omx_reg.h"
#include "omx_endpoint.h"

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xen_lib.h"
#include "omx_xenfront.h"
#include "omx_xenfront_endpoint.h"
#include "omx_xenfront_reg.h"

timers_t t_create_reg, t_destroy_reg, t_reg_seg, t_dereg_seg;
/* FIXME: Get rid of these copied functions from omx_reg.c */
static int
omx_wrapper_user_region_add_segment(const struct omx_cmd_user_segment *useg,
				    struct omx_user_region_segment *segment)
{
	unsigned long usegvaddr = useg->vaddr;
	unsigned long useglen = useg->len;
	struct page **pages;
	unsigned offset;
	unsigned long aligned_vaddr;
	unsigned long aligned_len;
	unsigned long nr_pages;
	int ret;

	dprintk_in();

	offset = usegvaddr & (~PAGE_MASK);
	aligned_vaddr = usegvaddr & PAGE_MASK;
	aligned_len = PAGE_ALIGN(offset + useglen);
	nr_pages = aligned_len >> PAGE_SHIFT;

	if (nr_pages > 4096) {
		pages = vmalloc(nr_pages * sizeof(struct page *));
		segment->vmalloced = 1;
	} else {
		pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
		segment->vmalloced = 0;
	}
	if (unlikely(!pages)) {
		printk(KERN_ERR
		       "Failed to allocate user region segment page array\n");
		ret = -ENOMEM;
		goto out;
	}
#if 0
#ifdef OMX_DRIVER_DEBUG
	printk_err("Memsetting pages\n");
	memset(pages, 0, nr_pages * sizeof(struct page *));
#endif
#endif

	segment->aligned_vaddr = aligned_vaddr;
	segment->first_page_offset = offset;
	segment->length = useglen;
	segment->nr_pages = nr_pages;
	segment->pinned_pages = 0;
	segment->pages = pages;

	ret = 0;

out:
	dprintk_out();
	return ret;
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
static inline void omx__wrapper_user_region_pin_new_segment(struct omx_user_region_pin_state
							    *pinstate)
{
	/*
	 * Called when pages is NULL, meaning that we finished the previous segment.
	 * The caller that set pages to NULL and increased the segment did not do this
	 * because it didn't know whether the next segment was valid. Now that we are
	 * here, we know it is valid since we are pinning more memory.
	 */
	struct omx_user_region_segment *segment = pinstate->segment;
	dprintk_in();
	pinstate->aligned_vaddr = segment->aligned_vaddr;
	pinstate->pages = segment->pages;
	pinstate->remaining = segment->length;
	pinstate->chunk_offset = segment->first_page_offset;
	dprintk_out();
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
void
omx__wrapper_user_region_pin_init(struct omx_user_region_pin_state *pinstate,
				  struct omx_user_region *region)
{
	dprintk_in();

	pinstate->region = region;
	pinstate->segment = &region->segments[0];
	pinstate->pages = NULL;	/* means that pin_new_segment() will do the init soon */
	pinstate->aligned_vaddr = 0;
	pinstate->remaining = 0;
	pinstate->chunk_offset = 0;
	pinstate->next_chunk_pages = omx_pin_chunk_pages_min;

	dprintk_out();
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
static int omx__wrapper_user_region_pin_add_chunk(struct omx_user_region_pin_state
						  *pinstate)
{
	struct omx_user_region *region = pinstate->region;
	struct omx_user_region_segment *seg = pinstate->segment;
	unsigned long aligned_vaddr;
	struct page **pages;
	unsigned long remaining;
	int chunk_offset;
	int chunk_length;
	int chunk_pages;
	int ret;

	dprintk_in();

	if (!pinstate->pages)
		omx__wrapper_user_region_pin_new_segment(pinstate);
	aligned_vaddr = pinstate->aligned_vaddr;
	pages = pinstate->pages;
	remaining = pinstate->remaining;
	chunk_offset = pinstate->chunk_offset;

	/* compute an estimated number of pages to pin */
	chunk_pages = pinstate->next_chunk_pages;
	/* increase the next number of pages to pin if below the max */
	if (chunk_pages < omx_pin_chunk_pages_max) {
		int next_chunk_pages = chunk_pages << 1;
		if (next_chunk_pages > omx_pin_chunk_pages_max)
			next_chunk_pages = omx_pin_chunk_pages_max;
		pinstate->next_chunk_pages = next_chunk_pages;
	}

	/* compute the corresponding length */
	if (chunk_offset + remaining <= chunk_pages << PAGE_SHIFT)
		chunk_length = remaining;
	else
		chunk_length = (chunk_pages << PAGE_SHIFT) - chunk_offset;

	/* compute the actual corresponding number of pages to pin */
	chunk_pages =
	    (chunk_offset + chunk_length + PAGE_SIZE - 1) >> PAGE_SHIFT;

	dprintk_deb
	    ("aligned_vaddr = %#lx, chunk_length = %u, remaining = %lu, chunk_offset = %u, chunk_pages = %u, pages = %p \n",
	     aligned_vaddr, chunk_length, remaining, chunk_offset, chunk_pages,
	     pages);
	ret = omx_get_user_pages_fast(aligned_vaddr, chunk_pages, 1, pages);
	if (unlikely(ret != chunk_pages)) {
		printk(KERN_ERR
		       "Failed to pin user buffer (%d pages at 0x%lx), get_user_pages returned %d\n",
		       chunk_pages, aligned_vaddr, ret);
		if (ret >= 0) {
			/* if some pages were acquired, release them */
			int i;
			for (i = 0; i < ret; i++)
				put_page(pages[i]);
			ret = -EFAULT;
		}
		goto out;
	}

	seg->pinned_pages += chunk_pages;
	region->total_registered_length += chunk_length;
	barrier();		/* needed for busy-waiter on total_registered_length */

	if (chunk_length < remaining) {
		/* keep the same segment */
		pinstate->aligned_vaddr =
		    aligned_vaddr + chunk_offset + chunk_length;
		pinstate->pages = pages + chunk_pages;
		pinstate->remaining = remaining - chunk_length;
		pinstate->chunk_offset = 0;

	} else {
		/* jump to next segment */
#ifdef OMX_DRIVER_DEBUG
		BUG_ON(seg->pinned_pages != seg->nr_pages);
#endif
		pinstate->pages = NULL;
		pinstate->segment = seg + 1;
	}

	ret = 0;
	goto out;

out:
	dprintk_out();
	return ret;
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
int omx__wrapper_user_region_pin_continue(struct omx_user_region_pin_state
					  *pinstate, unsigned long *length)
{
	struct omx_user_region *region = pinstate->region;
	unsigned long needed = *length;
	int ret;

	dprintk_in();
#ifdef OMX_DRIVER_DEBUG
	BUG_ON(region->status != OMX_USER_REGION_STATUS_PINNED);
#endif

	down_read(&current->mm->mmap_sem);
	while (region->total_registered_length < needed) {
		ret = omx__wrapper_user_region_pin_add_chunk(pinstate);
		if (ret < 0)
			goto out;
	}
	up_read(&current->mm->mmap_sem);
	*length = region->total_registered_length;

	dprintk_out();
	return 0;

out:
	up_read(&current->mm->mmap_sem);
	region->status = OMX_USER_REGION_STATUS_FAILED;
	dprintk_out();
	return ret;
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
static void
omx_wrapper_user_region_destroy_segment(struct omx_user_region_segment *segment)
{
	unsigned long i;

	dprintk_in();
	for (i = 0; i < segment->pinned_pages; i++)
		put_page(segment->pages[i]);

	if (segment->vmalloced)
		vfree(segment->pages);
	else
		kfree(segment->pages);
	dprintk_out();
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
static void
omx_wrapper_user_region_destroy_segments(struct omx_user_region *region)
{
	int i;

	dprintk_in();
	if (region->nr_vmalloc_segments)
		might_sleep();

	for (i = 0; i < region->nr_segments; i++)
		omx_wrapper_user_region_destroy_segment(&region->segments[i]);
	dprintk_out();
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
/*
 * when demand-pinning is disabled,
 * do a regular full pinning early
 */
static inline int
omx_wrapper_user_region_immediate_full_pin(struct omx_user_region *region)
{
	struct omx_user_region_pin_state pinstate;
	unsigned long needed = region->total_length;
	int ret = 0;

	dprintk_in();
#ifdef OMX_DRIVER_DEBUG
	BUG_ON(!omx_pin_synchronous);
	BUG_ON(region->status != OMX_USER_REGION_STATUS_NOT_PINNED);
#endif
	region->status = OMX_USER_REGION_STATUS_PINNED;

	omx__wrapper_user_region_pin_init(&pinstate, region);
	ret = omx__wrapper_user_region_pin_continue(&pinstate, &needed);

	dprintk_out();
	return ret;
}

/* FIXME: Get rid of these copied functions from omx_reg.c */
int
omx_ioctl_wrapper_user_region_create(struct omx_endpoint *endpoint,
				     void __user * uparam)
{
	struct omx_cmd_create_user_region cmd;
	struct omx_user_region *region;
	struct omx_user_region_segment *seg;
	struct omx_cmd_user_segment *usegs;
	int ret, i;

	dprintk_in();
	TIMER_START(&t_create_reg);
	if (unlikely(current->mm != endpoint->opener_mm)) {
		printk(KERN_ERR "Tried to register from another process\n");
		ret = -EFAULT;	/* the application does crap, behave as if it was a segfault */
		goto out;
	}

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Failed to read create region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (unlikely(cmd.id >= OMX_USER_REGION_MAX)) {
		printk_err("Cannot create invalid region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}

	/* get the list of segments */
	usegs = kmalloc(sizeof(struct omx_cmd_user_segment) * cmd.nr_segments,
			GFP_KERNEL);
	if (unlikely(!usegs)) {
		printk(KERN_ERR
		       "Failed to allocate segments for user region\n");
		ret = -ENOMEM;
		goto out;
	}
	ret = copy_from_user(usegs, (void __user *)(unsigned long)cmd.segments,
			     sizeof(struct omx_cmd_user_segment) *
			     cmd.nr_segments);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Failed to read create region cmd\n");
		ret = -EFAULT;
		goto out_with_usegs;
	}

	/* allocate the region */
	region = kzalloc(sizeof(struct omx_user_region)
			 +
			 cmd.nr_segments *
			 sizeof(struct omx_user_region_segment), GFP_KERNEL);
	if (unlikely(!region)) {
		printk(KERN_ERR "failed to allocate user region\n");
		ret = -ENOMEM;
		goto out_with_usegs;
	}

	kref_init(&region->refcount);
	kref_init(&region->xen_refcount);
	region->total_length = 0;
	region->nr_vmalloc_segments = 0;

	/* keep nr_segments exact so that we may call omx_user_region_destroy_segments safely */
	region->nr_segments = 0;

	/* allocate all segments */
	for (i = 0, seg = &region->segments[0]; i < cmd.nr_segments; i++) {
		dprintk(REG, "create region looking at useg %d len %lld\n",
			i, (unsigned long long)usegs[i].len);
		if (!usegs[i].len)
			continue;
		ret = omx_wrapper_user_region_add_segment(&usegs[i], seg);
		if (unlikely(ret < 0))
			goto out_with_region;

		if (seg->vmalloced)
			region->nr_vmalloc_segments++;
		region->nr_segments++;
		region->total_length += seg->length;
		dprintk(REG,
			"create region added new seg #%ld, total %ld length %ld\n",
			(unsigned long)(seg - &region->segments[0]),
			(unsigned long)region->nr_segments,
			region->total_length);
		seg++;
	}

	/* mark the region as non-registered yet */
	region->status = OMX_USER_REGION_STATUS_NOT_PINNED;
	region->total_registered_length = 0;

	if (omx_pin_synchronous) {
		/* pin the region */
		ret = omx_wrapper_user_region_immediate_full_pin(region);
		if (ret < 0) {
			dprintk(REG, "failed to pin user region\n");
			goto out_with_region;
		}
	}

	spin_lock(&endpoint->user_regions_lock);

	if (unlikely
	    (rcu_access_pointer(endpoint->user_regions[cmd.id]) != NULL)) {
		printk_err("Cannot create busy region %d\n", cmd.id);
		ret = -EBUSY;
		spin_unlock(&endpoint->user_regions_lock);
		goto out_with_region;
	}

	region->endpoint = endpoint;
	region->id = cmd.id;
	region->dirty = 0;
	rcu_assign_pointer(endpoint->user_regions[cmd.id], region);

	spin_unlock(&endpoint->user_regions_lock);

	kfree(usegs);
	ret = 0;
	goto out;

out_with_region:
	omx_wrapper_user_region_destroy_segments(region);
	kfree(region);
out_with_usegs:
	kfree(usegs);
out:
	TIMER_STOP(&t_create_reg);
	dprintk_out();
	return ret;
}

struct omx_xenfront_gref_cookie {
	struct list_head node;
	grant_ref_t gref_head;
	uint32_t count;
};

#if 1
static int omx_xen_gnttab_really_alloc_grant_references(struct omx_xenfront_info *fe, uint32_t count)
{
	int ret = 0, i = 0;
	grant_ref_t gref;
	struct omx_xenfront_gref_cookie *cookie;

	cookie = kmalloc(sizeof(struct omx_xenfront_gref_cookie), GFP_KERNEL);
	if (unlikely(!cookie)) {
		printk_err("Cannot allocate cookie!\n");
		ret = -ENOMEM;
		goto out;
	}
	ret = gnttab_alloc_grant_references(count, &cookie->gref_head);
	if (ret) {
		//printk_err("cannot allocate grant_references, count=%u\n", count);
		if (!cookie) {printk_err("WTF?\n");} else
		kfree(cookie);
		//ret = -ENOSPC;
		goto out;
	}
	cookie->count = count;

        write_lock(&fe->gref_cookies_freelock);
	list_add_tail(&cookie->node, &fe->gref_cookies_free);
        write_unlock(&fe->gref_cookies_freelock);
	dprintk_deb
	    ("allocated, and appended to list, %#lx, count = %u\n",
	     (unsigned long)cookie, count);


out:
	dprintk_out();
	return ret;
}
static void omx_xenfront_gref_put_cookie(struct omx_xenfront_info  * fe,
                             void *gref_cookie)
{
	struct omx_xenfront_gref_cookie *cookie = gref_cookie, *toremove = NULL;
        dprintk_in();
 //       write_lock(&fe->gref_cookies_freelock);

#if 0
	list_for_each_entry_safe(cookie, _cookie, &fe->gref_cookies_inuse, node) {
		dprintk_deb("gref_head = %u, cookie->gref_head=%u, cookie=%p\n", *gref_head, cookie->gref_head, (void*) cookie);
		if (*gref_head == cookie->gref_head + 1) {
			//list_del(&cookie->node);
			dprintk_deb("putting cookie%p with count =%u, gref_head=%u\n", (void*)cookie, cookie->count, *gref_head);
        		//list_move_tail(&cookie->node, &fe->gref_cookies_free);
			toremove = cookie;
			list_del(&cookie->node);
			break;
		}
	}
#endif
	dprintk_deb("putting gref_cookie =%p, count =%u head=%u\n", cookie, cookie->count, cookie->gref_head);
	toremove = cookie;
	if (!toremove)
		dprintk_deb("couldn't find cookie, with gref_head=%p\n", gref_cookie);
	else {
		write_lock(&fe->gref_cookies_inuselock);
		list_del(&cookie->node);
		write_unlock(&fe->gref_cookies_inuselock);

		gnttab_free_grant_references(toremove->gref_head);
		kfree(toremove);
	}

#if 0
	if (toremove) {
		dprintk_deb("putting cookie%p with count =%u, gref_head=%u\n", (void*)cookie, cookie->count, *gref_head);
		list_move_tail(&toremove->node, &fe->gref_cookies_free);
	} else
#endif

  //      write_unlock(&fe->gref_cookies_freelock);
        dprintk_out();
}

static int omx_xen_gnttab_free_grant_references(struct omx_xenfront_info *fe, grant_ref_t *gref_head, void**gref_cookie)
{
	int ret = 0, i = 0;

	dprintk_in();
	dprintk_deb("gref_head = %u, gref_cookie =%p\n", *gref_head, *gref_cookie);
	omx_xenfront_gref_put_cookie(fe, *gref_cookie);
out:
	dprintk_out();
	return ret;

}
#define XEN_GRANT_TIMEOUT 100000UL
static struct omx_xenfront_gref_cookie *omx_xenfront_gref_get_cookie(struct omx_xenfront_info * fe, uint32_t count)
{
        struct omx_xenfront_gref_cookie *cookie, *_cookie, *toreturn = NULL;
	int ret = 0;
	int i = 0;

        dprintk_in();

        dprintk_deb("want a gref cookie!\n");

        while ((volatile int)(list_empty(&fe->gref_cookies_free))) {
		ret = omx_xen_gnttab_really_alloc_grant_references(fe,count);
		/* if we can't alloc cookies, fail ungracefully */
		if (ret == -ENOMEM) {
			printk_err("we can't malloc!\n");
			goto out;
		}
		/* if we can't alloc grant references, wait for someone to free,
		 * die, with a timeout */
		if (ret == -ENOSPC && i < XEN_GRANT_TIMEOUT) {
			//write_unlock(&fe->gref_cookies_freelock);
			cpu_relax();
			i++;
			//write_lock(&fe->gref_cookies_freelock);
		}
        }

	//dprintk_inf("list not free, GO!\n");

        write_lock(&fe->gref_cookies_inuselock);
	list_for_each_entry_safe(cookie, _cookie, &fe->gref_cookies_free, node) {
		if (count <= cookie->count) {
			dprintk_deb("counts  match! %u, gref_head=%u cookie = %p\n", cookie->count, cookie->gref_head, (void*) cookie);
			list_move_tail(&cookie->node, &fe->gref_cookies_inuse);
			toreturn = cookie;
			break;
		}
		else dprintk_deb("counts don't match %u %u\n", count, cookie->count);
	}
        write_unlock(&fe->gref_cookies_inuselock);
        if (!toreturn) {
                printk_err("Error\n");
                goto out;
        }

        //write_unlock(&fe->gref_cookies_freelock);

        dprintk_deb("got it, %#010lx\n", (unsigned long)cookie);

out:
        dprintk_out();
        return toreturn;
}

static int omx_xen_gnttab_alloc_grant_references(struct omx_xenfront_info *fe, uint32_t count, grant_ref_t *gref_head, void **gref_cookie)
{
	int ret = 0, i = 0;
	struct omx_xenfront_gref_cookie *cookie;

	cookie = omx_xenfront_gref_get_cookie(fe, count);
	if (!cookie) {
		printk_err("cookie is NULL:(\n");
		ret = -EINVAL;
		goto out;
	}
	*gref_head = cookie->gref_head;
	*gref_cookie = cookie;

out:
	dprintk_out();
	return ret;

}

#endif
/* This is where Xen2MX specific functions begin */
int
omx_ioctl_xen_user_region_create(struct omx_endpoint *endpoint,
				 void __user * uparam)
{
	int ret = 0, i = 0;
	struct omx_cmd_create_user_region cmd;
	struct omx_user_region *region;
	struct omx_user_region_segment *seg;
	struct omx_xenfront_info *fe;
	struct xenbus_device *dev;
	struct omx_xenif_request *ring_req;
	struct omx_ring_msg_register_user_segment *ring_seg;
	uint32_t request_id;

	dprintk_in();

	fe = endpoint->fe;
	BUG_ON(!fe);

	dev = fe->xbdev;
	BUG_ON(!dev);

	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Failed to read create region cmd\n");
		ret = -EFAULT;
		goto out;
	}

	/* Create the frontend user region */
	ret = omx_ioctl_wrapper_user_region_create(endpoint, uparam);
	if (unlikely(ret < 0)) {
		//printk_err(
		//       "Cannot access a non-existing region %d\n", cmd.id);
		//ret = -EINVAL;
		goto out;
	}

	/* Get a hold on the region pointer */
	spin_lock(&endpoint->user_regions_lock);
	if (unlikely
	    ((region =
	      rcu_access_pointer(endpoint->user_regions[cmd.id])) == NULL)) {
		printk_err(
		       "Cannot access a non-existing region %d\n", cmd.id);
		ret = -EINVAL;
		spin_unlock(&endpoint->user_regions_lock);
		goto out;
	}
	spin_unlock(&endpoint->user_regions_lock);
#if 0
	if (cmd.nr_segments > 1) {
		printk_err("we don't currently support more that 1 segment!, segments = %d\n", cmd.nr_segments);
		ret = -EINVAL;
		goto out;
	}
#endif

	/* Prepare the message to the backend */

#if 0
	spin_lock(&region->status_lock);
	//region->status = OMX_USER_REGION_STATUS_REGISTERING;
	endpoint->special_status = OMX_USER_REGION_STATUS_REGISTERING;
	spin_unlock(&region->status_lock);
#endif
	ring_req = omx_ring_get_request(fe);
	request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
	fe->requests[request_id] = OMX_USER_REGION_STATUS_REGISTERING;
	ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_XEN_CREATE_USER_REGION;
	/* Ultra safe */
	//memset(&ring_req->data.cur, 0, sizeof(ring_req->data.cur));
	ring_req->data.cur.nr_segments = cmd.nr_segments;
	ring_req->data.cur.id = cmd.id;
	ring_req->data.cur.eid = endpoint->endpoint_index;

	/* Handle each segment separately */
	for (i = 0, seg = &region->segments[0]; i < cmd.nr_segments; i++) {
		int j;
		int k;
		uint8_t nr_parts;
		uint32_t gref_size;
		grant_ref_t gref;
		unsigned long mfn;
		void *gref_vaddr;
		uint16_t gref_offset;
		struct page *gref_page;

		if (!seg) {printk_err ("seg is NULL\n"); ret = -EINVAL; goto out;}

		gref_size = (seg->nr_pages);
		nr_parts =
		    (seg->nr_pages * sizeof(uint32_t) + PAGE_SIZE -
		     1) / PAGE_SIZE + 1;

		seg->nr_parts = nr_parts;
#ifdef EXTRA_DEBUG_OMX
		/* Let the user know */
		if (nr_parts > 1) {
			dprintk_deb
			    ("splitting gref list to multiple pages nr_parts = %d\n",
			     nr_parts);
		}
#endif

		gref_vaddr =
		    (void *)__get_free_pages(GFP_KERNEL,
					     get_order(nr_parts * PAGE_SIZE));

		gref_offset = (unsigned long)gref_vaddr & ~PAGE_MASK;

		seg->gref_list = (uint32_t *) gref_vaddr;


		/* Allocate a set of grant references */
		if ((ret =
		     omx_xen_gnttab_alloc_grant_references(fe, gref_size + nr_parts,
						   &seg->gref_head, &seg->gref_cookie))) {
			printk_err("Cannot allocate %d grant references\n",
				   gref_size + nr_parts);
			goto out;
		}
		spin_lock_init(&seg->status_lock);

#ifdef EXTRA_DEBUG_OMX
		/* init gref_list for debugging! */
		for (j = 0; j < (gref_size); j++) {
			seg->gref_list[j] = 0xdead;
		}
#endif

		/* Grant each gref_list page */
		for (k = 0; k < nr_parts; k++) {
			unsigned long tmp_vaddr =
			    (unsigned long)gref_vaddr + k * PAGE_SIZE;

			gref_page = virt_to_page(tmp_vaddr);

			gref =
			    gnttab_claim_grant_reference(&seg->gref_head);
			mfn = pfn_to_mfn(page_to_pfn(gref_page));

			gnttab_grant_foreign_access_ref(gref, 0, mfn, 0);

			seg->all_gref[k] = gref;
			dprintk_deb("gref[%d] = %#x\n", k, seg->all_gref[k]);
			dprintk_deb
			    ("gref= %d, gref_list is @%#lx, tmp_vaddr = %#lx, page=%p, mfn=%#lx\n",
			     gref, (unsigned long)seg->gref_list, tmp_vaddr,
			     (void *)gref_page, mfn);
		}

		/* Skip empty segments */
		if (!seg->length)
			continue;

		/* Grant each segment page. Remember, these pages are pinned by the standard
		 * region_create call */
		for (j = 0; j < seg->nr_pages; j++) {
			struct page *single_page;
			unsigned long mfn, pfn;
			single_page = seg->pages[j];
			/* FIXME: which is the correct way to get the mfn ?
			 * pfn_to_mfn(page_to_pfn()) or virt_to_mfn(page_address()) ? */
			pfn = page_to_pfn(single_page);
			mfn = pfn_to_mfn(pfn);
			seg->gref_list[j] =
			    gnttab_claim_grant_reference(&seg->gref_head);
			if (seg->gref_list[j] <= 0)
				printk_err("ref is %d\n", seg->gref_list[j]);
			gnttab_grant_foreign_access_ref(seg->gref_list[j], 0,
							mfn, 0);
		}
		/* FIXME: Do we need a barrier here ? */
		wmb();
		/* Prepare the message to the backend for segment registration */
		spin_lock(&seg->status_lock);
		seg->status = OMX_USER_SEGMENT_STATUS_GRANTING;
		spin_unlock(&seg->status_lock);

		//memset(&ring_req->data.cus, 0, sizeof(ring_req->data.cus));
		ring_seg = &ring_req->data.cur.segs[i];

		ring_seg->sid = i;
		ring_seg->rid = cmd.id;
		ring_seg->eid = endpoint->endpoint_index;
		ring_seg->aligned_vaddr = seg->aligned_vaddr;
		ring_seg->first_page_offset = seg->first_page_offset;
		ring_seg->nr_pages = seg->nr_pages;

		/* FIXME: is memcpy better ? */
		for (k = 0; k < seg->nr_parts; k++) {
			ring_seg->gref[k] = seg->all_gref[k];
			dprintk_deb("ring_gref[%d] = %#x\n", k,
				    ring_seg->gref[k]);
			dprintk_deb("gref[%d] = %#x\n", k, seg->all_gref[k]);
		}
		ring_seg->gref_offset = gref_offset;
		ring_seg->nr_parts = seg->nr_parts;
		ring_seg->nr_grefs = 1024;
		ring_seg->length = seg->length;

		seg++;
	}

	//dump_xen_ring_msg_create_user_region(&ring_req->data.cur);
	omx_poke_dom0(fe, ring_req);
	rmb();
	//ndelay(1000);
	/* FIXME: find a better way to get notified that a backend response has come */
	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_USER_REGION_STATUS_REGISTERING,
	     &region->status_lock)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->requests[request_id]== OMX_USER_REGION_STATUS_FAILED) {
		printk_err
		    ("Received failure from backend, will abort, status = %d\n",
		     region->status);
		ret = -EINVAL;
		goto out;
	}


	ret = 0;
out:
	dprintk_out();
	return ret;
}

/*
 * FIXME: Called when a pull request has been completed (marked DONE).
 * This is a temporary hack: The backend services PULL handles independently,
 * releasing/freeing its region structures without letting the frontend know,
 * so the only way for the frontend to be consistent, is to just release the region
 * and the segment grants outside the main IOCTL function for region destroy (which
 * lets the backend know).
 */

int
omx_xen_user_region_release(struct omx_endpoint *endpoint, uint32_t region_id)
{
	int ret = 0, i = 0;
	struct omx_cmd_destroy_user_region cmd;
	struct omx_user_region *region;
	struct omx_user_region_segment *seg;
	dprintk_in();

	/* Get a hold on the region pointer */
	spin_lock(&endpoint->user_regions_lock);
	if (unlikely
	    ((region =
	      rcu_access_pointer(endpoint->user_regions[region_id])) == NULL)) {
		printk(KERN_ERR
		       "Cannot access a non-existing region %d\n", cmd.id);
		ret = -EINVAL;
		spin_unlock(&endpoint->user_regions_lock);
		goto out;
	}
	spin_unlock(&endpoint->user_regions_lock);

	/* Loop around segments to release grant references */
	for (i = 0, seg = &region->segments[0]; i < region->nr_segments; i++) {
		int j;
		int k;
#ifdef EXTRA_DEBUG_OMX
		uint8_t *redo;
#endif
		if (!seg->length)
			continue;

#ifdef EXTRA_DEBUG_OMX
		/* FIXME: Crazy hack to retry once if pages are still in use in the backend */
		redo = kzalloc(sizeof(uint8_t) * seg->nr_pages, GFP_KERNEL);
#endif

		/* Release each page reference separately */
		for (j = 0; j < seg->nr_pages; j++) {
			struct page *single_page;
			unsigned long mfn, pfn;

			single_page = seg->pages[j];
			/* FIXME: which is the correct way to get the mfn ?
			 * pfn_to_mfn(page_to_pfn()) or virt_to_mfn(page_address()) ? */
			pfn = page_to_pfn(single_page);
			mfn = pfn_to_mfn(pfn);

			ret = gnttab_query_foreign_access(seg->gref_list[j]);
			if (ret) {
				printk_inf
				    ("gref_list[%d] = %u, mfn=%#lx is still in use by the backend!\n",
				     j, seg->gref_list[j], mfn);
#ifdef EXTRA_DEBUG_OMX
				redo[j] = 1;
#endif
				continue;
			}
			ret =
			    gnttab_end_foreign_access_ref(seg->gref_list[j], 0);
			if (!ret) {
				printk_inf
				    ("Can't end foreign access for gref_list[%d] = %u, mfn=%#lx\n",
				     j, seg->gref_list[j], mfn);
#ifdef EXTRA_DEBUG_OMX
				redo[j] = 1;
#endif
				continue;
			}
			gnttab_release_grant_reference(&seg->gref_head,
						       seg->gref_list[j]);
		}

#ifdef EXTRA_DEBUG_OMX
		/* FIXME: This is where we idiotically retry  */
		for (j = 0; j < seg->nr_pages; j++) {
			struct page *single_page;
			void *vaddr;
			unsigned long mfn;

			if (!redo[j])
				continue;

			single_page = seg->pages[j];
			vaddr = page_address(single_page);
			mfn = virt_to_mfn(vaddr);

			ret = gnttab_query_foreign_access(seg->gref_list[j]);
			if (ret) {
				printk_inf
				    ("gref_list[%d] = %u, mfn=%#lx is still in use by the backend!\n",
				     j, seg->gref_list[j], mfn);
				goto out;
			}

			ret =
			    gnttab_end_foreign_access_ref(seg->gref_list[j], 0);
			if (!ret) {
				printk_inf
				    ("Can't end foreign access for gref_list[%d] = %u, mfn=%#lx\n",
				     j, seg->gref_list[j], mfn);
			gnttab_release_grant_reference(&seg->gref_head,
						       seg->gref_list[j]);
				/* FIXME: Do we really need to fail with -EBUSY ? */
				ret = -EBUSY;
				goto out;
			}
		}

		/* We no longer need this buffer space */
		kfree(redo);
#endif

		/* Release all gref_list pages */
		for (k = 0; k < seg->nr_parts; k++) {
			dprintk_deb
			    ("ending foreign access for part = %d, gref=%#x\n",
			     k, seg->all_gref[k]);
			ret = gnttab_query_foreign_access(seg->all_gref[k]);
			if (ret) {
				printk_inf
				    ("gref_list[%d] = %u, is still in use by the backend!\n",
				     k, seg->all_gref[k]);
			}
			ret =
			    gnttab_end_foreign_access_ref(seg->all_gref[k], 0);
			if (!ret) {
				printk_inf
				    ("Can't end foreign access for gref_list[%d] = %u, is still in use by the backend!\n",
				     k, seg->all_gref[k]);
				/* FIXME: Do we really need to fail with -EBUSY ? */
				ret = -EBUSY;
				goto out;
			}
			gnttab_release_grant_reference(&seg->gref_head,
						       seg->all_gref[k]);
		}
		omx_xen_gnttab_free_grant_references(endpoint->fe, &seg->gref_head, &seg->gref_cookie);

		/* Since we use __get_free_pages, we call free_pages here */
		free_pages((unsigned long)seg->gref_list,
			   get_order(((seg->nr_parts * PAGE_SIZE))));

		seg++;
	}

#if 0
	/* Destroy the region locally */
	cmd.id = region_id;
	omx_ioctl_user_region_destroy(endpoint, &cmd);
#endif

	ret = 0;
out:
	dprintk_out();
	return ret;
}
void
__omx_xen_user_region_last_release(struct kref * kref)
{
	int ret = 0, i = 0;
	struct omx_cmd_destroy_user_region cmd;
	//struct omx_user_region *region;
	struct omx_user_region_segment *seg;
	struct omx_user_region * region = container_of(kref, struct omx_user_region, xen_refcount);
	struct omx_endpoint *endpoint = region->endpoint;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	struct omx_ring_msg_deregister_user_segment *ring_seg;
	dprintk_in();

#if 0
	/* FIXME: find a better way to get notified that a backend response has come */
	if ((ret = wait_for_backend_response
	    (&region->status, OMX_USER_REGION_STATUS_DEREGISTERING,
	     &region->status_lock)) < 0) {
		printk_err("Failed to wait\n");
	//	ret = -EINVAL;
		goto out;
	}
#endif
	//dprintk_inf("%s: region = %p\n", __func__, (void*) region);
	/* Loop around segments to release grant references */
	for (i = 0, seg = &region->segments[0]; i < region->nr_segments; i++) {
		int j;
		int k;
#ifdef EXTRA_DEBUG_OMX
		uint8_t *redo;
#endif
		if (!seg->length)
			continue;

#ifdef EXTRA_DEBUG_OMX
		/* FIXME: Crazy hack to retry once if pages are still in use in the backend */
		redo = kzalloc(sizeof(uint8_t) * seg->nr_pages, GFP_KERNEL);
#endif

		/* Release each page reference separately */
		for (j = 0; j < seg->nr_pages; j++) {
			struct page *single_page;
			unsigned long mfn, pfn;

			single_page = seg->pages[j];
			pfn = page_to_pfn(single_page);
			mfn = pfn_to_mfn(pfn);

			ret = gnttab_query_foreign_access(seg->gref_list[j]);
			if (ret) {
				printk_inf
				    ("gref_list[%d] = %u, mfn=%#lx is still in use by the backend!\n",
				     j, seg->gref_list[j], mfn);
#ifdef EXTRA_DEBUG_OMX
				redo[j] = 1;
#endif
				continue;
			}
			ret =
			    gnttab_end_foreign_access_ref(seg->gref_list[j], 0);
			if (!ret) {
				printk_inf
				    ("Can't end foreign access for gref_list[%d] = %u, mfn=%#lx\n",
				     j, seg->gref_list[j], mfn);
#ifdef EXTRA_DEBUG_OMX
				redo[j] = 1;
#endif
				continue;
			}
			gnttab_release_grant_reference(&seg->gref_head,
						       seg->gref_list[j]);
		}

#ifdef EXTRA_DEBUG_OMX
		/* FIXME: This is where we idiotically retry. Like there's a chance that the
		 * second time we try, we may succeed ;S */
		for (j = 0; j < seg->nr_pages; j++) {
			struct page *single_page;
			void *vaddr;
			unsigned long mfn;

			if (!redo[j])
				continue;

			single_page = seg->pages[j];
			vaddr = page_address(single_page);
			mfn = virt_to_mfn(vaddr);

			//dprintk_deb("gref_list[%d] = %u, mfn=%#lx\n", j, seg->gref_list[j], mfn);
			if (gnttab_query_foreign_access(seg->gref_list[j]))
				printk_inf
				    ("gref_list[%d] = %u, mfn=%#lx is still in use by the backend!\n",
				     j, seg->gref_list[j], mfn);
			ret =
			    gnttab_end_foreign_access_ref(seg->gref_list[j], 0);
			if (!ret)
				printk_inf
				    ("Can't end foreign access for gref_list[%d] = %u, mfn=%#lx\n",
				     j, seg->gref_list[j], mfn);
			gnttab_release_grant_reference(&seg->gref_head,
						       seg->gref_list[j]);
		}

		/* We no longer need this buffer space */
		kfree(redo);
#endif

		/* Release all gref_list pages */
		for (k = 0; k < seg->nr_parts; k++) {
			dprintk_deb
			    ("ending foreign access for part = %d, gref=%#x\n",
			     k, seg->all_gref[k]);
			ret = gnttab_query_foreign_access(seg->all_gref[k]);
			if (ret) {
				printk_inf
				    ("gref_list[%d] = %u, is still in use by the backend!\n",
				     k, seg->all_gref[k]);
			}
			ret =
			    gnttab_end_foreign_access_ref(seg->all_gref[k], 0);
			if (!ret) {
				printk_inf
				    ("Can't end foreign access for gref_list[%d] = %u, is still in use by the backend!\n",
				     k, seg->all_gref[k]);
			}
			gnttab_release_grant_reference(&seg->gref_head,
						       seg->all_gref[k]);
		}
		omx_xen_gnttab_free_grant_references(endpoint->fe, &seg->gref_head, &seg->gref_cookie);

		/* Since we use __get_free_pages, we call free_pages here */
		free_pages((unsigned long)seg->gref_list,
			   get_order(((seg->nr_parts * PAGE_SIZE))));

		seg++;
	}

	/* FIXME: Pass it on to the standard destroy IOCTL function */
	spin_lock(&endpoint->user_regions_lock);
	region->status = OMX_USER_REGION_STATUS_PINNED;
	spin_unlock(&endpoint->user_regions_lock);

out_from_backend:
	cmd.id = region->id;
	omx_ioctl_user_region_destroy(endpoint, &cmd);
out:
	dprintk_out();
	//return ret;
}

static void
__omx_xen_user_region_rcu_release_callback(struct rcu_head *xen_rcu_head)
{
	struct omx_user_region * region = container_of(xen_rcu_head, struct omx_user_region, xen_rcu_head);
	dprintk_in();
	kref_put(&region->xen_refcount, __omx_xen_user_region_last_release);
	dprintk_out();
}

int
omx_ioctl_xen_user_region_destroy(struct omx_endpoint *endpoint,
				  void __user * uparam)
{
	int ret = 0, i = 0;
	struct omx_cmd_destroy_user_region cmd;
	struct omx_user_region *region;
	struct omx_user_region_segment *seg;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	struct omx_ring_msg_deregister_user_segment *ring_seg;
	uint32_t request_id;
	dprintk_in();

	TIMER_START(&t_destroy_reg);
	ret = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Failed to read create region cmd\n");
		ret = -EFAULT;
		goto out;
	}


	/* Get a hold on the region pointer.
	 * FIXME: If we fail, its possible that the region
	 * is already released by the previous function (if its a PULL request message).
	 * So this is not neccesarily an error *
	 * FIXME: Is this still true ? */
	spin_lock(&endpoint->user_regions_lock);
	if (unlikely
	    ((region =
	      rcu_access_pointer(endpoint->user_regions[cmd.id])) == NULL)) {
		spin_unlock(&endpoint->user_regions_lock);
		printk_err("Cannot unregister unknown region %d\n", cmd.id);
		ret = -EINVAL;
		goto out;
	}
	spin_unlock(&endpoint->user_regions_lock);


	//dprintk_inf("%s: region = %p\n", __func__, (void*) region);
	/* Prepare the message to the backend */
#if 0
	spin_lock(&region->status_lock);
	region->status = OMX_USER_REGION_STATUS_DEREGISTERING;
	spin_unlock(&region->status_lock);
#endif

	/* FIXME: maybe create a static inline function for this stuff ? */
	ring_req = omx_ring_get_request(fe);
	request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
	fe->requests[request_id] = OMX_USER_REGION_STATUS_DEREGISTERING;
	ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_XEN_DESTROY_USER_REGION;
	/* Ultra safe */
	//memset(&ring_req->data.dur, 0, sizeof(ring_req->data.dur));
	ring_req->data.dur.eid = endpoint->endpoint_index;
	ring_req->data.dur.id = region->id;
	ring_req->data.dur.nr_segments = region->nr_segments;
	ring_req->data.dur.region = (uint64_t) region;
	/* Loop around segments to release grant references */
	for (i = 0, seg = &region->segments[0]; i < region->nr_segments; i++) {

		if (!seg->length)
			continue;
		ring_seg = &ring_req->data.dur.segs[i];
		//memset(&ring_req->data.dus, 0, sizeof(ring_req->data.dus));
		ring_seg->sid = i;
		ring_seg->rid = cmd.id;
		ring_seg->eid = endpoint->endpoint_index;
	}


	dprintk_deb("send request to de-register region id=%d\n", cmd.id);
	//dump_xen_ring_msg_destroy_user_region(&ring_req->data.dur);

	omx_poke_dom0(fe, ring_req);

	/* FIXME: find a better way to get notified that a backend response has come */
	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_USER_REGION_STATUS_DEREGISTERING,
	     &region->status_lock)) < 0) {
		printk_err("Failed to wait\n");
	//	ret = -EINVAL;
		goto out;
	}

	//call_rcu(&region->xen_rcu_head, __omx_xen_user_region_rcu_release_callback);

	if (fe->requests[request_id] == OMX_USER_REGION_STATUS_FAILED) {
		printk_err
		    ("Received failure from backend, will abort, status = %d\n",
		     region->status);
		ret = -EINVAL;
		goto out;
	}
	kref_put(&region->xen_refcount, __omx_xen_user_region_last_release);

        RCU_INIT_POINTER(endpoint->user_regions[cmd.id], NULL);

	ret = 0;
out:
	TIMER_STOP(&t_destroy_reg);
	dprintk_out();
	return ret;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
