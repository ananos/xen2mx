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

#include <stdarg.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/xenbus.h>
#include <xen/page.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/e820.h>

//#define TIMERS_ENABLED
#include "omx_xen_timers.h"

#include "omx_reg.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_endpoint.h"

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_reg.h"
#include "omx_xenback_event.h"

timers_t t_reg_seg, t_create_reg, t_dereg_seg, t_destroy_reg;

int omx_xen_deregister_user_segment(omx_xenif_t * omx_xenif, uint32_t id,
				    uint32_t sid, uint8_t eid)
{
	struct gnttab_unmap_grant_ref ops;
	struct backend_info *be = omx_xenif->be;
	struct omxback_dev *dev = be->omxdev;
	struct omx_endpoint *endpoint = dev->endpoints[eid];
	struct omx_xen_user_region *region;
	struct omx_xen_user_region_segment *seg;
	int i, k, ret = 0;
	unsigned int level;

	dprintk_in();

	TIMER_START(&t_dereg_seg);
	if (eid < 0 && eid >= 255) {
		printk_err
		    ("Wrong endpoint number (%u) check your frontend/backend communication!\n",
		     eid);
		ret = -EINVAL;
		goto out;
	}

	region = rcu_dereference_protected(endpoint->xen_regions[id], 1);
	if (unlikely(!region)) {
		dprintk_deb(
		       "Open-MX: Cannot access non-existing region %d\n", id);
		//ret = -EINVAL;
		goto out;
	}
	seg = &region->segments[sid];
	for (i = 0; i < seg->nr_pages; i++) {
		struct page *page = seg->pages[i];
		grant_handle_t handle = seg->handles[i];

		//dprintk_deb("putting page %#lx, addr=%#lx\n", (unsigned long)page, page_address(page));

		omx_xen_unmap_page(handle, page);
#ifdef OMX_XEN_COOKIES
		omx_xen_page_put_cookie(omx_xenif, seg->cookies[i]);
#endif

	}

	for (k = 0; k < seg->nr_parts; k++) {
#ifdef EXTRA_DEBUG_OMX
		if (!seg->vm_gref) {
			printk(KERN_ERR "vm_gref is NULL\n");
			ret = -EFAULT;
			goto out;
		}
		if (!seg->vm_gref[k]) {
			printk(KERN_ERR "vm_gref[%d] is NULL\n", k);
			ret = -EFAULT;
			goto out;
		}
		if (!seg->vm_gref[k]->addr) {
			printk(KERN_ERR "vm_gref[%d]->addr is NULL\n", k);
			ret = -EFAULT;
			goto out;
		}
		if (!seg->all_handle[k]) {
			printk(KERN_ERR "all_handle[%d] is NULL\n", k);
			ret = -EINVAL;
			goto out;
		}
#endif
		gnttab_set_unmap_op(&ops, (unsigned long)seg->vm_gref[k]->addr,
				    GNTMAP_host_map | GNTMAP_contains_pte,
				    seg->all_handle[k]);
		ops.host_addr =
		    arbitrary_virt_to_machine(lookup_address
					      ((unsigned long)(seg->vm_gref[k]->
							       addr),
					       &level)).maddr;

		dprintk_deb("putting vm_area[%d] %#lx, handle = %#x \n", k,
			    (unsigned long)seg->vm_gref[k], seg->all_handle[k]);
		if (HYPERVISOR_grant_table_op
		    (GNTTABOP_unmap_grant_ref, &ops, 1))
			BUG();
		if (ops.status) {
			printk_err
			    ("HYPERVISOR unmap grant ref[%d] failed status = %d",
			     k, ops.status);

			ret = ops.status;
			goto out;
		}
#if 1
		if (ops.status == GNTST_okay)
			free_vm_area(seg->vm_gref[k]);
#endif
	}

	dprintk_deb("freeing handles %#lx\n", (unsigned long)seg->handles);

	kfree(seg->gref_list);
	kfree(seg->handles);
	kfree(seg->pages);
#ifdef OMX_XEN_COOKIES
	kfree(seg->cookies);
#endif

out:
	TIMER_STOP(&t_dereg_seg);
	dprintk_out();
	return ret;

}

int omx_xen_destroy_user_region(omx_xenif_t * omx_xenif, uint32_t id,
				uint32_t seqnum, uint8_t eid)
{
	struct backend_info *be = omx_xenif->be;
	struct omxback_dev *dev = be->omxdev;
	struct omx_endpoint *endpoint = dev->endpoints[eid];
	struct omx_xen_user_region *region;
	int ret = 0;

	dprintk_in();

	TIMER_START(&t_destroy_reg);
	if (eid >= 0 && eid < 255) {
		endpoint = dev->endpoints[eid];
	} else {
		printk_err
		    ("Wrong endpoint number (%u) check your frontend/backend communication!\n",
		     eid);
		ret = -EINVAL;
		goto out;
	}

	region = rcu_dereference_protected(endpoint->xen_regions[id], 1);
	if (unlikely(!region)) {
		dprintk_deb(
		       "Open-MX: Cannot access non-existing region %d\n", id);
		//ret = -EINVAL;
		goto out;
	}

	rcu_assign_pointer(endpoint->xen_regions[region->id], NULL);
	//omx_xen_user_region_release(region);
	kfree(region);
out:
	TIMER_STOP(&t_destroy_reg);
	dprintk_out();
	return ret;

}

static int omx_xen_accept_gref_list(omx_xenif_t * omx_xenif,
				    struct omx_xen_user_region_segment *seg,
				    uint32_t gref, void **vaddr, uint8_t part)
{
	int ret = 0;
	struct backend_info *be = omx_xenif->be;
	struct vm_struct *area;
	pte_t *pte;
	struct gnttab_map_grant_ref ops = {
		.flags = GNTMAP_host_map | GNTMAP_contains_pte,
		//.flags = GNTMAP_host_map,
		.ref = gref,
		.dom = be->remoteDomain,
	};

	dprintk_in();

	area = alloc_vm_area(PAGE_SIZE, &pte);
	if (!area) {
		ret = -ENOMEM;
		goto out;
	}

	seg->vm_gref[part] = area;

	ops.host_addr = arbitrary_virt_to_machine(pte).maddr;

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &ops, 1)) {
		printk_err("HYPERVISOR map grant ref failed");
		ret = -ENOSYS;
		goto out;
	}
	dprintk_deb("addr=%#lx, mfn=%#lx, kaddr=%#lx\n",
		    (unsigned long)area->addr, ops.dev_bus_addr >> PAGE_SHIFT,
		    ops.host_addr);
	if (ops.status) {
		printk_err("HYPERVISOR map grant ref failed status = %d",
			   ops.status);

		ret = ops.status;
		goto out;
	}

	dprintk_deb("gref_offset = %#x\n", seg->gref_offset);
	*vaddr = (area->addr + seg->gref_offset);

	ret = ops.handle;
#if 0
	for (i = 0; i < (size + 2); i++) {
		dprintk_deb("gref_list[%d] = %u\n", i,
			    *(((uint32_t *) * vaddr) + i));
	}
#endif

	seg->all_handle[part] = ops.handle;
	dprintk_deb("vaddr = %p, area->addr=%p, handle[%d]=%d\n", vaddr,
		    area->addr, part, seg->all_handle[part]);

out:
	dprintk_out();
	return ret;
}

int omx_xen_unmap_page(uint32_t handle, struct page *page)
{
	int ret = 0;
	struct gnttab_unmap_grant_ref ops;

	//dprintk_in();

#ifdef EXTRA_DEBUG_OMX
	if (!page) {
		printk_err("page is null\n");
		ret = -EINVAL;
		goto real_out;
	}
#endif
#if 0
	ops = kmalloc(sizeof(struct gnttab_unmap_grant_ref), GFP_KERNEL);
	if (!ops) {
		printk_err("ops is null\n");
		ret = -ENOMEM;
		goto real_out;
	}
#endif
	gnttab_set_unmap_op(&ops, (unsigned long)pfn_to_kaddr(page_to_pfn(page)),
			    GNTMAP_host_map, handle);

#if 1

	//if (ops2)
	//      kfree(ops2);
	//gnttab_set_unmap_op(&ops, (unsigned long)pfn_to_kaddr(page_to_pfn(page)), GNTMAP_host_map, handle);
	//ops.host_addr = (unsigned long) pfn_to_kaddr(page_to_pfn(page));

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &ops, 1))
		BUG();
	if (ops.status) {
		printk_err
		    ("HYPERVISOR unmap specific grant ref failed status = %d",
		     ops.status);

		ret = ops.status;
		goto out;
	}
#endif
#if 1
	ret = m2p_remove_override(page, false);
	if (ret) {
		printk_err("m2p faileD!!!!!!!\n");
		ret = -EFAULT;
		goto out;
	}
#endif
#if 0
	if (ops->status == GNTST_okay)
		//put_page(page);
		if (page) {
			//__free_page(page);
		}
#endif

out:
	//kfree(ops);
//real_out:
	//dprintk_out();
	return ret;
}

int omx_xen_map_page(struct backend_info *be, uint32_t gref, void **vaddr,
		     uint32_t * handle, struct page **retpage,
		     struct omx_xen_page_cookie **cookie)
{
	int ret = 0;
	struct page *page;
	uint32_t mfn;
	struct gnttab_map_grant_ref ops;
	struct omx_xen_page_cookie *page_cookie;

	ops.flags = GNTMAP_host_map;
	ops.ref = gref;
	ops.dom = be->remoteDomain;

	*vaddr = NULL;

	if (!cookie) {
		page = alloc_page(GFP_KERNEL);
	} else {
		page_cookie = omx_xen_page_get_cookie(be->omx_xenif);
		if (!page_cookie) {
			printk_err("Not a valid cookie\n");
			ret = -EINVAL;
			goto out;
		}
		page = page_cookie->page;
		*cookie = page_cookie;
	}

	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	ops.host_addr = (unsigned long)pfn_to_kaddr(page_to_pfn(page));

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &ops, 1)) {
		printk_err("HYPERVISOR map grant ref failed");
		ret = -ENOSYS;
		goto out;
	}
	if (ops.status) {
		printk_err("HYPERVISOR map grant ref failed status = %d",
			   ops.status);

		ret = ops.status;
		goto out;
	}
	mfn = ops.dev_bus_addr >> PAGE_SHIFT;
	//mfn = PFN_DOWN(ops->dev_bus_addr);
	//}

#if 1
	ret = m2p_add_override(mfn, page, NULL);
	if (ret) {
		printk_err("m2p failed!, ret = %d\n", ret);
		goto out;
	}
#endif
	if (retpage)
		*retpage = page;
	*vaddr = page_address(page);
	*handle = ops.handle;

	//kfree(ops);

out:
	//dprintk_out();
	return ret;
}

int omx_xen_register_user_segment(omx_xenif_t * omx_xenif,
				  struct omx_ring_msg_register_user_segment *req)
{

	struct backend_info *be = omx_xenif->be;
	void *vaddr = NULL;
	uint32_t **gref_list;
	struct page **page_list;
	struct omxback_dev *omxdev = be->omxdev;
	struct omx_endpoint *endpoint;
	struct omx_xen_user_region *region;
	struct omx_xen_user_region_segment *seg;
	int ret = 0;
	int i = 0, k = 0;
	uint8_t eid, nr_parts;
	uint16_t first_page_offset, gref_offset;
	uint32_t sid, id, nr_grefs, nr_pages, length,
	    gref[OMX_XEN_GRANT_PAGES_MAX];
	uint64_t domU_vaddr;
	int idx = 0, sidx = 0;

	dprintk_in();

	TIMER_START(&t_reg_seg);
	sid = req->sid;
	id = req->rid;
	eid = req->eid;
	domU_vaddr = req->aligned_vaddr;
	nr_grefs = req->nr_grefs;
	nr_pages = req->nr_pages;
	nr_parts = req->nr_parts;
	length = req->length;
	dprintk_deb("nr_parts = %#x\n", nr_parts);
	for (k = 0; k < nr_parts; k++) {
		gref[k] = req->gref[k];
		dprintk_deb("printing gref = %lu\n", gref[k]);
	}
	gref_offset = req->gref_offset;
	first_page_offset = req->first_page_offset;
	endpoint = omxdev->endpoints[eid];

	region = rcu_dereference_protected(endpoint->xen_regions[id], 1);
	if (unlikely(!region)) {
		printk_err(KERN_ERR "Cannot access non-existing region %d\n",
			   id);
		ret = -EINVAL;
		goto out;
	}
	dprintk_deb("Got region @%#lx id=%u\n", (unsigned long)region, id);

	seg = &region->segments[sid];
	if (unlikely(!seg)) {
		printk(KERN_ERR "Cannot access non-existing segment %d\n", sid);
		ret = -EINVAL;
		goto out;
	}
	dprintk_deb("Got segment @%#lx id=%u\n", (unsigned long)seg, sid);

	seg->gref_offset = gref_offset;
	dprintk_deb
	    ("Offset of actual list of grant references (in the frontend) = %#x\n",
	     gref_offset);

	for (k = 0; k < nr_parts; k++) {
		seg->all_gref[k] = gref[k];
		dprintk_deb("grant reference for list of grefs = %#x\n",
			    gref[k]);
	}
	seg->nr_parts = nr_parts;
	dprintk_deb("parts of gref list = %#x\n", nr_parts);

	gref_list = kzalloc(sizeof(uint32_t *) * nr_parts, GFP_ATOMIC);
	if (!gref_list) {
		ret = -ENOMEM;
		printk_err("gref list is cannot be allocated, ENOMEM!!!\n");
		goto out;
	}

	page_list = kzalloc(sizeof(struct page *) * nr_pages, GFP_ATOMIC);
	if (!page_list) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}
#ifdef OMX_XEN_COOKIES
	seg->cookies =
	    kzalloc(sizeof(struct omx_xen_page_cookie *) * nr_pages,
		    GFP_ATOMIC);
	if (!seg->cookies) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}
#endif

	seg->handles = kzalloc(sizeof(grant_ref_t) * nr_pages, GFP_ATOMIC);
	if (!seg->handles) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}

	for (k = 0; k < nr_parts; k++) {
		ret =
		    omx_xen_accept_gref_list(omx_xenif, seg, gref[k], &vaddr,
					     k);
		if (ret < 0) {
			printk_err("Cannot accept gref list, = %d\n", ret);
			goto out;
		}

		gref_list[k] = (uint32_t *) vaddr;
		if (!gref_list) {
			printk_err("gref_list is NULL!!!, = %p\n", gref_list);
			ret = -ENOSYS;
			goto out;
		}
	}
	seg->gref_list = gref_list;

	seg->nr_pages = nr_pages;
	seg->first_page_offset = first_page_offset;

	i = 0;
	idx = 0;
	sidx = 0;
	while (i < nr_pages) {
		void *tmp_vaddr;
		struct page *page;
		if (sidx % 256 == 0)
			dprintk_deb("gref_list[%d][%d] = %#x\n", idx, sidx,
				    gref_list[idx][sidx]);

		ret =
		    omx_xen_map_page(omx_xenif->be, gref_list[idx][sidx],
				     &tmp_vaddr, &seg->handles[i], NULL,
#ifdef OMX_XEN_COOKIES
				     &seg->cookies[i]);
#else
				     NULL);
#endif
		if (ret) {
			printk_err("map page failed!, ret = %d\n", ret);
			goto out;
		}

#ifdef EXTRA_DEBUG_OMX
		if (sidx % 256 == 0)
			dprintk_deb("%#lx\n", tmp_vaddr);
		if (!virt_addr_valid(tmp_vaddr)) {
			ret = -EINVAL;
			goto out;
		} else {
#endif
			page = virt_to_page(tmp_vaddr);
#ifdef EXTRA_DEBUG_OMX
		}
		if (!page)
			printk_err("No page found:(\n");
		else {
			if (sidx % 256 == 0)
				dprintk_deb("page=%#lx\n", (unsigned long)page);
#endif
			page_list[i] = page;
#ifdef EXTRA_DEBUG_OMX
		}
#endif
		i++;
		if ((unlikely(i % nr_grefs == 0))) {
			idx++;
			sidx = 0;
		} else {
			sidx++;
		}
	}
	seg->pages = page_list;
	seg->nr_pages = i;
	seg->length = length;
	region->total_length += length;
	dprintk_deb("total_length = %#lx, nrpages=%lu, pages = %#lx\n",
		    region->total_length, seg->nr_pages,
		    (unsigned long)seg->pages);
	goto all_ok;
out:
	printk_err("error registering, try to debug MORE!!!!\n");

all_ok:
	TIMER_STOP(&t_reg_seg);
	dprintk_out();
	return ret;
}

int omx_xen_create_user_region(omx_xenif_t * omx_xenif, uint32_t id,
			       uint64_t vaddr, uint32_t nr_segments,
			       uint32_t nr_pages, uint32_t nr_grefs,
			       uint8_t eid)
{

	struct backend_info *be = omx_xenif->be;
	struct omxback_dev *omxdev = be->omxdev;
	struct omx_endpoint *endpoint = omxdev->endpoints[eid];
	struct omx_xen_user_region *region;
	int ret = 0;

	dprintk_in();
	TIMER_START(&t_create_reg);
	//udelay(1000);
	/* allocate the relevant region */
	region =
	    kzalloc(sizeof(struct omx_xen_user_region) +
		    nr_segments * sizeof(struct omx_xen_user_region_segment),
		    GFP_KERNEL);
	if (!region) {
		printk_err
		    ("No memory to allocate the region/segment buffers\n");
		ret = -ENOMEM;
		goto out;
	}

	/* init stuff needed :S */
	kref_init(&region->refcount);
	region->total_length = 0;
	region->nr_vmalloc_segments = 0;

	region->total_registered_length = 0;

	region->id = id;
	region->nr_segments = nr_segments;
	region->eid = eid;

	region->endpoint = endpoint;
	region->dirty = 0;

	if (unlikely(rcu_access_pointer(endpoint->xen_regions[id]) != NULL)) {
		printk(KERN_ERR "Cannot create busy region %d\n", id);
		ret = -EBUSY;
		goto out;
	}

	rcu_assign_pointer(endpoint->xen_regions[id], region);

out:
	TIMER_STOP(&t_create_reg);
	dprintk_out();
	return ret;
}

/* Various region/segment handler functions */

void
omx_xen_user_region_destroy_segments(struct omx_xen_user_region *region,
				     struct omx_endpoint *endpoint)
{
	int i;

	dprintk_in();
	if (!endpoint) {
		printk_err("endpoint is null!!\n");
		return;
	}
	for (i = 0; i < region->nr_segments; i++)
		omx_xen_deregister_user_segment(endpoint->be->omx_xenif,
						region->id, i,
						endpoint->endpoint_index);

	dprintk_out();
}

/* Called when the last reference on the region is released */
void __omx_xen_user_region_last_release(struct kref *kref)
{
	dprintk_in();
#if 0
	struct omx_xen_user_region *region =
	    container_of(kref, struct omx_xen_user_region, refcount);
	//struct omx_endpoint *endpoint = region->endpoint;

	dprintk_deb("releasing the last reference on region %p, %#x\n", region,
		    region->id);

	/* FIXME, we can't release the segments region from the backend, we need to get
	 * a frontend kick first:S Hence, we just decrease the refcount... Really impressive huh? */
//#if 0
	if (region->nr_vmalloc_segments && in_interrupt()) {
		OMX_INIT_WORK(&region->destroy_work,
			      omx_region_destroy_workfunc, region);
		schedule_work(&region->destroy_work);
	} else {
		omx_user_region_destroy_segments(region);
		kfree(region);
	}
//#endif
	//printk(KERN_INFO "Will free now the specified region\n");
//#if 0
	if (endpoint) {
		omx_xen_user_region_destroy_segments(region, endpoint);
		rcu_assign_pointer(endpoint->xen_regions[region->id], NULL);
	}
	kfree(region);
//#endif

#endif
	dprintk_out();
}

void omx_xen_user_region_release(struct omx_xen_user_region *region)
{
	kref_put(&region->refcount, __omx_xen_user_region_last_release);
}

/* maybe be called from bottom halves */
struct omx_xen_user_region *omx_xen_user_region_acquire(const struct
							omx_endpoint *endpoint,
							uint32_t rdma_id)
{
	struct omx_xen_user_region *region;

	dprintk_in();
	if (unlikely(rdma_id >= OMX_USER_REGION_MAX)) {
		printk_err("rdma_id = %#x\n", rdma_id);
		goto out;
	}

	rcu_read_lock();

	region = rcu_dereference(endpoint->xen_regions[rdma_id]);
	if (unlikely(!region)) {
		printk_err("region is NULL!!\n");
		goto out_with_rcu_lock;
	}

	kref_get(&region->refcount);

	rcu_read_unlock();
	dprintk_out();
	return region;

out_with_rcu_lock:
	rcu_read_unlock();
out:
	dprintk_out();
	return NULL;
}

int
omx_xen_user_region_offset_cache_init(struct omx_xen_user_region *region,
				      struct omx_user_region_offset_cache
				      *cache, unsigned long offset,
				      unsigned long length)
{
	struct omx_xen_user_region_segment *seg;
	unsigned long segoff;
	int ret = 0;
	dprintk_in();

	if (unlikely
	    (!region->nr_segments || offset + length > region->total_length)) {
		ret = -EINVAL;
		printk_err("Invalid Offset\n");
		goto out;
	}

	dprintk(REG, "Cache -> XEN = 1\n");
	cache->xen = 1;
	cache->xregion = region;

	if (unlikely(region->nr_segments > 1)) {
		unsigned long tmp;
		printk(KERN_INFO
		       "It is highly unlikely to cross this code path\n");
		ret = -EINVAL;
		goto out;

		/* vectorial callbacks */
		cache->append_pages_to_skb =
		    omx_user_region_offset_cache_vect_append_callback;
		cache->copy_pages_to_buf =
		    omx_user_region_offset_cache_vect_copy_callback;
#ifdef OMX_HAVE_DMA_ENGINE
		cache->dma_memcpy_from_pg =
		    omx_user_region_offset_cache_dma_vect_memcpy_from_pg_callback;
		cache->dma_memcpy_from_buf =
		    omx_user_region_offset_cache_dma_vect_memcpy_from_buf_callback;
#endif

		/* find the segment */
		for (tmp = 0, seg =
		     (struct omx_xen_user_region_segment *)&region->segments[0];
		     tmp + seg->length <= offset; tmp += seg->length, seg++) ;

		/* find the segment offset */
		segoff = offset - tmp;

	} else {
		/* vectorial callbacks */
		cache->append_pages_to_skb =
		    omx_user_region_offset_cache_contig_append_callback;
		cache->copy_pages_to_buf =
		    omx_user_region_offset_cache_contig_copy_callback;
#ifdef OMX_HAVE_DMA_ENGINE
		cache->dma_memcpy_from_pg =
		    omx_user_region_offset_cache_dma_contig_memcpy_from_pg_callback;
		cache->dma_memcpy_from_buf =
		    omx_user_region_offset_cache_dma_contig_memcpy_from_buf_callback;
#endif

		/* use the first segment */
		seg =
		    (struct omx_xen_user_region_segment *)&region->segments[0];
		segoff = offset;
	}

	/* setup the segment and offset */
	cache->xseg = seg;
	cache->segoff = segoff;

	dprintk_deb("seg->pages@%#lx \n", (unsigned long)seg->pages);
	dprintk_deb("seg@%#lx, segoff = %#lx, first_page_offset=%#x\n",
		    (unsigned long)seg, segoff, seg->first_page_offset);
#ifdef EXTRA_DEBUG_OMX
	if (seg->first_page_offset > PAGE_SIZE) {
		printk_err("Something is really really wrong:S\n");
		ret = -EINVAL;
		goto out;
	}
#endif
#ifdef EXTRA_DEBUG_OMX
	if (seg->pages) {
#endif
		/* find the page and offset */
		cache->page =
		    &seg->
		    pages[(segoff + seg->first_page_offset) >> PAGE_SHIFT];
		cache->pageoff =
		    (segoff + seg->first_page_offset) & (~PAGE_MASK);

		dprintk_deb
		    ("initialized region offset cache to seg (@%#lx) #%ld offset %ld page (@%#lx) #%ld offset %d\n",
		     (unsigned long)(seg),
		     (unsigned long)(seg - &region->segments[0]), segoff,
		     (unsigned long)(cache->page),
		     (unsigned long)(cache->page - &seg->pages[0]),
		     cache->pageoff);
#ifdef EXTRA_DEBUG_OMX
	} else {
		printk_err("Error, seg->pages is NULL\n");
		ret = -EINVAL;
		goto out;
	}
#endif

#ifdef OMX_DRIVER_DEBUG
	cache->current_offset = offset;
	cache->max_offset = offset + length;
#endif

out:
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
