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

#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/xenbus.h>

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
#include "omx_xenback_endpoint.h"

/* FIXME: unused functions for now */
int omx_xen_fill_frags_from_queue_offset(struct omx_endpoint *endpoint,
					 struct sk_buff *skb,
					 unsigned int offset, uint32_t length)
{
	int i = 0, ret = 0;
	int page_idx;
	unsigned int current_offset;
	uint16_t first_page_offset;
	uint32_t copy_length;
	struct page *page;

	dprintk_in();
	page_idx = offset >> PAGE_SHIFT;
	first_page_offset = offset & ~PAGE_MASK;
	copy_length = length;
	current_offset = offset;

	if (length < 1) {
		printk_err("Are you joking? length = %d\n", length);
		ret = -EINVAL;
		goto out;
	}
	if (!endpoint) {
		printk_err("Are you joking? endpoint is NULL\n");
		ret = -EINVAL;
		goto out;
	}

	if (!skb) {
		printk_err("Are you joking? skb is NULL\n");
		ret = -EINVAL;
		goto out;
	}

	dprintk_deb("remaining length = %#lx, page_idx = %d, offset=%#x\n",
		    copy_length, page_idx, offset);
	i = 0;
	do {
		uint32_t chunk = copy_length;
		page_idx = current_offset >> PAGE_SHIFT;

		chunk = copy_length > PAGE_SIZE ? PAGE_SIZE : copy_length;

		if (first_page_offset && chunk > PAGE_SIZE)
			chunk = PAGE_SIZE - first_page_offset;
		dprintk_deb
		    ("remaining length = %#lx, page_idx = %d, first_page_offset=%#x\n",
		     chunk, page_idx, first_page_offset);

		page = endpoint->sendq_pages[page_idx];
		//get_page(page);
#if 0
		skb_fill_page_desc(skb, i, page, current_offset & (~PAGE_MASK),
				   chunk);
#endif
		i++;

		copy_length -= chunk;
		current_offset += chunk;
		first_page_offset = 0;
		dprintk_deb("remaining = %#x, next page=%d\n", chunk, page_idx);
	} while (copy_length > 0);

out:
	dprintk_out();
	return ret;

}

/* FIXME: unused functions for now */
int omx_xen_copy_from_queue_offset(struct omx_endpoint *endpoint, void *dest,
				   unsigned int offset, uint32_t length)
{
	void *vaddr;
	int ret = 0;
	int page_idx = 0;
	unsigned int current_offset;
	uint16_t first_page_offset;
	uint32_t copy_length;

	dprintk_in();
	page_idx = offset >> PAGE_SHIFT;
	first_page_offset = offset & ~PAGE_MASK;
	copy_length = length;
	current_offset = offset;

	if (length < 1) {
		printk_err("Are you joking? length = %d\n", length);
		ret = -EINVAL;
		goto out;
	}
	if (!endpoint) {
		printk_err("Are you joking? endpoint is NULL\n");
		ret = -EINVAL;
		goto out;
	}

	if (!dest) {
		printk_err("Are you joking? dest is NULL\n");
		ret = -EINVAL;
		goto out;
	}

	dprintk_deb("remaining length = %#lx, page_idx = %d, offset=%#x\n",
		    copy_length, page_idx, offset);
	do {
		uint32_t chunk = copy_length;
		page_idx = current_offset >> PAGE_SHIFT;

		chunk = copy_length > PAGE_SIZE ? PAGE_SIZE : copy_length;

		if (first_page_offset && chunk > PAGE_SIZE)
			chunk = PAGE_SIZE - first_page_offset;
		dprintk_deb
		    ("remaining length = %#lx, page_idx = %d, first_page_offset=%#x\n",
		     chunk, page_idx, first_page_offset);

		vaddr = page_address(endpoint->xen_sendq_pages[page_idx]);
#if 0
		memcpy(dest + i * PAGE_SIZE, vaddr + first_page_offset, chunk);
#endif

		copy_length -= chunk;
		current_offset += chunk;
		first_page_offset = 0;
		dprintk_deb("remaining = %#x, next page=%d\n", chunk, page_idx);
	} while (copy_length > 0);

out:
	dprintk_out();
	return ret;

}

/* accept the send/recv queue grants */
static int omx_xen_accept_queue_grefs(omx_xenif_t * omx_xenif,
				      struct omx_endpoint *endpoint,
				      uint32_t gref, struct vm_struct **vm_area,
				      void **vaddr, uint32_t * handle,
				      uint16_t offset)
{
	int ret = 0;
	struct backend_info *be = omx_xenif->be;
	struct vm_struct *area;
	pte_t *pte;
	struct gnttab_map_grant_ref ops = {
		.flags = GNTMAP_host_map | GNTMAP_contains_pte,
		.ref = gref,
		.dom = be->remoteDomain,
	};

	dprintk_in();

	area = alloc_vm_area(PAGE_SIZE, &pte);
	if (!area) {
		ret = -ENOMEM;
		goto out;
	}

	*vm_area = area;

	ops.host_addr = arbitrary_virt_to_machine(pte).maddr;

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &ops, 1)) {
		printk_err("HYPERVISOR map endpoint grant ref failed");
		ret = -ENOSYS;
		goto out;
	}
	dprintk_deb("addr=%#lx, mfn=%#lx, kaddr=%#lx\n",
		    (unsigned long)area->addr, ops.dev_bus_addr >> PAGE_SHIFT,
		    ops.host_addr);
	if (ops.status) {
		printk_err
		    ("HYPERVISOR map endpoint grant ref failed status = %d",
		     ops.status);

		ret = ops.status;
		goto out;
	}

	dprintk_deb("gref_offset = %#x\n", offset);
	*vaddr = (area->addr + offset);

	ret = ops.handle;

	*handle = ops.handle;
	dprintk_deb("vaddr = %p, area->addr=%p, handle=%d\n", *vaddr,
		    area->addr, *handle);

out:
	dprintk_out();
	return ret;
}

int omx_xen_endpoint_accept_resources(struct omx_endpoint *endpoint,
				      struct omx_xenif_request *req)
{
	int ret = 0, i;
	struct backend_info *be = endpoint->be;
	omx_xenif_t *omx_xenif = be->omx_xenif;
	uint32_t sendq_gref_size;
	uint32_t recvq_gref_size;
	uint16_t egref_sendq_offset;
	uint16_t egref_recvq_offset;
	uint16_t endpoint_offset;
	grant_ref_t sendq_gref;
	grant_ref_t recvq_gref;
	grant_ref_t endpoint_gref;
	grant_ref_t *sendq_gref_list;
	grant_ref_t *recvq_gref_list;
	struct page **sendq_page_list;
	struct page **recvq_page_list;
	uint32_t *xen_sendq_handles;
	uint32_t *xen_recvq_handles;
	void *void_vaddr;

	dprintk_in();

	sendq_gref_size = req->data.endpoint.sendq_gref_size;
	recvq_gref_size = req->data.endpoint.recvq_gref_size;
	egref_sendq_offset = req->data.endpoint.egref_sendq_offset;
	egref_recvq_offset = req->data.endpoint.egref_recvq_offset;
	endpoint_offset = req->data.endpoint.endpoint_offset;
	sendq_gref = req->data.endpoint.sendq_gref;
	recvq_gref = req->data.endpoint.recvq_gref;
	endpoint_gref = req->data.endpoint.endpoint_gref;

	endpoint->xen_sendq_gref_size = sendq_gref_size;
	endpoint->xen_recvq_gref_size = recvq_gref_size;

	ret =
	    omx_xen_accept_queue_grefs(omx_xenif, endpoint, endpoint_gref,
				       &endpoint->endpoint_vm,
				       &void_vaddr,
				       &endpoint->endpoint_handle,
				       endpoint_offset);
	if (ret < 0) {
		printk_err("Failed to accept endpoint vaddr... ret = %d\n",
			   ret);
		goto out;
	}
	endpoint->fe_endpoint = (struct omx_endpoint *)void_vaddr;

	sendq_page_list =
	    kmalloc(sizeof(struct page *) * sendq_gref_size, GFP_KERNEL);
	if (!sendq_page_list) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}

	recvq_page_list =
	    kmalloc(sizeof(struct page *) * recvq_gref_size, GFP_KERNEL);
	if (!recvq_page_list) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}

	xen_sendq_handles =
	    kmalloc(sizeof(uint32_t) * sendq_gref_size, GFP_KERNEL);
	if (!xen_sendq_handles) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}
	xen_recvq_handles =
	    kmalloc(sizeof(uint32_t) * recvq_gref_size, GFP_KERNEL);
	if (!xen_recvq_handles) {
		ret = -ENOMEM;
		printk_err(" page list is NULL, ENOMEM!!!\n");
		goto out;
	}

	ret =
	    omx_xen_accept_queue_grefs(omx_xenif, endpoint, sendq_gref,
				       &endpoint->xen_sendq_vm,
				       &void_vaddr,
				       &endpoint->xen_sendq_handle,
				       egref_sendq_offset);
	if (ret < 0) {
		printk_err("Failed to accept send queue grefs ret = %d\n", ret);
		goto out;
	}
	sendq_gref_list = (uint32_t *) void_vaddr;
	endpoint->xen_sendq_list = sendq_gref_list;

	ret =
	    omx_xen_accept_queue_grefs(omx_xenif, endpoint, recvq_gref,
				       &endpoint->xen_recvq_vm,
				       &void_vaddr,
				       &endpoint->xen_recvq_handle,
				       egref_recvq_offset);
	if (ret < 0) {
		printk_err("Failed to accept recvq queue grefs ret = %d\n",
			   ret);
		goto out;
	}

	recvq_gref_list = (uint32_t *) void_vaddr;
	endpoint->xen_recvq_list = recvq_gref_list;

	i = 0;
	while (i < sendq_gref_size) {
		void *tmp_vaddr;
		struct page *page = NULL;
		grant_ref_t gref;
		grant_handle_t handle;

		gref = sendq_gref_list[i];
		dprintk_deb("gref[%d] = %#x\n", i, gref);
		ret =
		    omx_xen_map_page(be, gref, &tmp_vaddr, &handle, NULL, NULL);
		if (ret) {
			printk_err("map page failed!, ret = %d\n", ret);
			goto out;
		}
		xen_sendq_handles[i] = handle;
		dprintk_deb("handle[%d] = %#x\n", i, handle);
		if (!virt_addr_valid(tmp_vaddr)) {
			ret = -EINVAL;
			printk_err("Virt addr invalid:(\n");
			//goto out;
		} else {
			page = virt_to_page(tmp_vaddr);
		}
		if (!page)
			printk_err("No page found:(\n");
		else {
			sendq_page_list[i] = page;
		}
		dprintk_deb("page[%d] = %#lx\n", i, (unsigned long)page);

		i++;
	}
	endpoint->xen_sendq_pages = sendq_page_list;
	endpoint->xen_sendq_handles = xen_sendq_handles;

	/* FIXME:
	 * That's what we should be able to do. Map the entire set of physical pages,
	 * holding the data from the frontend to a  virtually contiguous space, addressable
	 * from the dom0 kernel
	 */
#if 0
	endpoint->xen_sendq =
	    vmap(sendq_page_list, sendq_gref_size, VM_MAP, PAGE_KERNEL);
#endif

	i = 0;
	while (i < recvq_gref_size) {
		void *tmp_vaddr;
		struct page *page = NULL;

		ret =
		    omx_xen_map_page(be, recvq_gref_list[i], &tmp_vaddr,
				     &xen_recvq_handles[i], NULL, NULL);
		if (ret) {
			printk_err("map page failed!, ret = %d\n", ret);
			goto out;
		}
		if (!virt_addr_valid(tmp_vaddr)) {
			ret = -EINVAL;
			printk_err("Virt addr invalid:(\n");
			//goto out;
		} else {
			page = virt_to_page(tmp_vaddr);
		}
		if (!page)
			printk_err("No page found:(\n");
		else {
			recvq_page_list[i] = page;
		}

		i++;
	}
	endpoint->xen_recvq_pages = recvq_page_list;
	endpoint->xen_recvq_handles = xen_recvq_handles;

	/* FIXME:
	 * That's what we should be able to do. Map the entire set of physical pages,
	 * holding the data from the frontend to a  virtually contiguous space, addressable
	 * from the dom0 kernel
	 */
#if 0
	endpoint->xen_recvq =
	    vmap(recvq_page_list, recvq_gref_size, VM_MAP, PAGE_KERNEL);
#endif

out:
	dprintk_out();
	return ret;
}

int omx_xen_endpoint_release_resources(struct omx_endpoint *endpoint,
				       struct omx_xenif_request *req)
{
	int ret = 0, i;
	struct gnttab_unmap_grant_ref ops;

	//struct backend_info *be = endpoint->be;
	uint32_t sendq_gref_size;
	uint32_t recvq_gref_size;
	unsigned int level;

	dprintk_in();

	sendq_gref_size = endpoint->xen_sendq_gref_size;
	recvq_gref_size = endpoint->xen_recvq_gref_size;

	if (!endpoint->xen_sendq_pages || !endpoint->xen_recvq_pages) {
		printk_err("The list of xen_recv/sendq_pages is null\n");
		ret = -EINVAL;
		goto out;
	}
#if 0
	if (!endpoint->xen_sendq) {
		printk_err("vmap'd space is NULL\n");
		ret = -EINVAL;
		goto out;
	}
	vunmap(endpoint->xen_sendq);
#endif

	for (i = 0; i < sendq_gref_size; i++) {
		struct page *page;
		grant_handle_t handle;
		page = endpoint->xen_sendq_pages[i];
		if (!page) {
			printk_err("sendq_page[%d] is NULL\n", i);
			ret = -EINVAL;
			goto out;
		}
		handle = endpoint->xen_sendq_handles[i];

		dprintk_deb("putting page %#lx, addr=%#lx\n",
			    (unsigned long)page, page_address(page));
		ret = omx_xen_unmap_page(handle, page);
		if (ret) {
			printk_err("sendq_page[%d] is NULL\n", i);
			ret = -EINVAL;
			goto out;
		}
	}
	gnttab_set_unmap_op(&ops, (unsigned long)endpoint->xen_sendq_vm->addr,
			    GNTMAP_host_map | GNTMAP_contains_pte,
			    endpoint->xen_sendq_handle);
	ops.host_addr =
	    arbitrary_virt_to_machine(lookup_address
				      ((unsigned long)(endpoint->xen_sendq_vm->
						       addr), &level)).maddr;

	dprintk_deb("putting sendq vm_area %#lx, handle = %#x \n",
		    (unsigned long)endpoint->xen_sendq_vm,
		    endpoint->xen_sendq_handle);
	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &ops, 1))
		printk_err("hypervisor command failed:S\n");	//BUG();
	if (ops.status) {
		printk_err
		    ("HYPERVISOR unmap sendq grant ref failed status = %d",
		     ops.status);

		ret = ops.status;
		goto out;
	}
#if 0
	if (!endpoint->xen_recvq) {
		printk_err("recv vmap'd space is NULL\n");
		ret = -EINVAL;
		goto out;
	}
	vunmap(endpoint->xen_recvq);
#endif
	for (i = 0; i < recvq_gref_size; i++) {
		struct page *page;
		grant_handle_t handle;
		page = endpoint->xen_recvq_pages[i];
		if (!page) {
			printk_err("recvq_page[%d] is NULL\n", i);
			ret = -EINVAL;
			goto out;
		}
		handle = endpoint->xen_recvq_handles[i];

		dprintk_deb("putting page %#lx, addr=%#lx\n",
			    (unsigned long)page, page_address(page));
		ret = omx_xen_unmap_page(handle, page);
		if (ret) {
			printk_err("sendq_page[%d] is NULL\n", i);
			ret = -EINVAL;
			goto out;
		}
	}
	gnttab_set_unmap_op(&ops, (unsigned long)endpoint->xen_recvq_vm->addr,
			    GNTMAP_host_map | GNTMAP_contains_pte,
			    endpoint->xen_recvq_handle);
	ops.host_addr =
	    arbitrary_virt_to_machine(lookup_address
				      ((unsigned long)(endpoint->xen_recvq_vm->
						       addr), &level)).maddr;

	dprintk_deb("putting recvq vm_area %#lx, handle = %#x \n",
		    (unsigned long)endpoint->xen_recvq_vm,
		    endpoint->xen_recvq_handle);
	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &ops, 1))
		printk_err("hypervisor command failed:S\n");	//  BUG();
	if (ops.status) {
		printk_err
		    ("HYPERVISOR unmap recvq grant ref failed status = %d",
		     ops.status);

		ret = ops.status;
		goto out;
	}

	gnttab_set_unmap_op(&ops, (unsigned long)endpoint->endpoint_vm->addr,
			    GNTMAP_host_map | GNTMAP_contains_pte,
			    endpoint->endpoint_handle);
	ops.host_addr =
	    arbitrary_virt_to_machine(lookup_address
				      ((unsigned long)(endpoint->endpoint_vm->
						       addr), &level)).maddr;

	dprintk_deb("putting recvq vm_area %#lx, handle = %#x \n",
		    (unsigned long)endpoint->endpoint_vm,
		    endpoint->endpoint_handle);
	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &ops, 1))
		printk_err("hypervisor command failed:S\n");	//  BUG();
	if (ops.status) {
		printk_err
		    ("HYPERVISOR unmap recvq grant ref failed status = %d",
		     ops.status);

		ret = ops.status;
		goto out;
	}
	kfree(endpoint->xen_sendq_pages);
	kfree(endpoint->xen_recvq_pages);
	kfree(endpoint->xen_sendq_handles);
	kfree(endpoint->xen_recvq_handles);
	free_vm_area(endpoint->xen_sendq_vm);
	free_vm_area(endpoint->xen_recvq_vm);
	free_vm_area(endpoint->endpoint_vm);

out:
	dprintk_out();
	return ret;
}

int omx_xen_endpoint_open(struct backend_info *be,
			  struct omx_xenif_request *req)
{
	struct omx_endpoint *endpoint;
	struct omxback_dev *omxdev;
	uint8_t bidx;
	uint8_t idx;
	uint32_t session_id;
	int ret = 0;

	dprintk_in();

	BUG_ON(!req);
	BUG_ON(!be);

	bidx = req->board_index;
	idx = req->eid;
	session_id = req->data.endpoint.session_id;

	omxdev = be->omxdev;
	dprintk_deb("simulating omx_endpoint_open (%d,%d)\n", bidx, idx);

	endpoint = omxdev->endpoints[idx];
	dprintk_deb("Got endpoint %d @ %#lx\n", idx, (unsigned long)endpoint);
	dprintk_deb("Got endpoint %d @ %#lx\n", idx, (unsigned long)endpoint);
	ret = -EBUSY;
	BUG_ON(!endpoint);
	kref_init(&endpoint->refcount);
	spin_lock_init(&endpoint->status_lock);

	spin_lock_irq(&endpoint->status_lock);
	if (endpoint->status != OMX_ENDPOINT_STATUS_FREE) {
		printk_err("Endpoint NOT free, status =%d\n", endpoint->status);
		if (endpoint->status == OMX_ENDPOINT_STATUS_OK) {
			printk_err("but that's OK\n");
			spin_unlock_irq(&endpoint->status_lock);
			ret = 0;
			goto out;
		}
		spin_unlock_irq(&endpoint->status_lock);
		ret = -EBUSY;
		goto out;
	}
	endpoint->status = OMX_ENDPOINT_STATUS_INITIALIZING;
	spin_unlock_irq(&endpoint->status_lock);
	/* alloc internal fields  */
	if ((ret = omx_endpoint_alloc_resources(endpoint)) < 0) {
		printk_err("Something went wrong with "
			    "allocating endpoint resources, ret = %d\n", ret);
		ret = -EFAULT;
		goto out;
	}

	if ((ret = omx_xen_endpoint_accept_resources(endpoint, req)) < 0) {
		printk_err("Something went wrong with "
			    "accepting endpoint resources, ret = %d\n", ret);
		ret = -EFAULT;
		goto out;
	}
	/* attach the endpoint to the iface */
	endpoint->board_index = bidx;
	endpoint->endpoint_index = idx;
	endpoint->session_id = session_id;
	spin_lock_irq(&endpoint->status_lock);
	ret = omx_iface_attach_endpoint(endpoint);
	if (ret < 0) {
		printk_err("Something went wrong with "
			    "attaching endpoint to iface\n");
		spin_unlock_irq(&endpoint->status_lock);
		goto out;
	}
	spin_unlock_irq(&endpoint->status_lock);
	endpoint->opener_pid = current->pid;
	strncpy(endpoint->opener_comm, current->comm, TASK_COMM_LEN);

	/* By now, the endpoint should be considered Initialized.
	 * We can safely set its status OK
	 */
	spin_lock_irq(&endpoint->status_lock);
	endpoint->status = OMX_ENDPOINT_STATUS_OK;
	spin_unlock_irq(&endpoint->status_lock);
	endpoint->xen = 1;

	ret = 0;
out:
	dprintk_out();
	return ret;
}

static void __omx_xen_endpoint_last_release(struct kref *kref)
{
	struct omx_endpoint *endpoint =
	    container_of(kref, struct omx_endpoint, refcount);
	struct omx_iface *iface = endpoint->iface;

	dprintk_deb
	    ("releasing the last reference on endpoint %d for iface %s (%s)\n",
	     endpoint->endpoint_index, iface->peer.hostname,
	     iface->eth_ifp->name);

	endpoint->iface = NULL;
	omx_iface_release(iface);

/* FIXME is this correct ? */
#if 1
	if (in_interrupt()) {
		OMX_INIT_WORK(&endpoint->destroy_work,
			      omx_endpoint_destroy_workfunc, endpoint);
		schedule_work(&endpoint->destroy_work);
	} else {
		omx_endpoint_free_resources(endpoint);
		kfree(endpoint);
	}
#endif
}

int omx_xen_endpoint_close(struct backend_info *be,
			   struct omx_xenif_request *req)
{
	struct omx_endpoint *endpoint;
	struct omxback_dev *omxdev;
	uint8_t bidx;
	uint8_t idx;
	int ret = 0;

	dprintk_in();

	BUG_ON(!req);
	BUG_ON(!be);

	bidx = req->board_index;
	idx = req->eid;

	omxdev = be->omxdev;

	//dprintk_deb("received ioctl for close_endpoint\n");
	dprintk_deb("simluating omx_endpoint_close (%d,%d)\n", bidx, idx);
	endpoint = be->omxdev->endpoints[idx];
	//printk_inf("%s: Got endpoint %d @ %#lx\n", __func__, idx, (unsigned long)endpoint);
	ret = -EINVAL;
	BUG_ON(!endpoint);

	spin_lock_irq(&endpoint->status_lock);
	if (endpoint->status == OMX_ENDPOINT_STATUS_FREE) {
		spin_unlock_irq(&endpoint->status_lock);
		printk_err("Endpoint Already free\n");
		ret = 0;
		goto out;
	}

	if (endpoint->status != OMX_ENDPOINT_STATUS_OK) {
		spin_unlock_irq(&endpoint->status_lock);
		printk_err("Endpoint not OK\n");
		goto out;
	}
	endpoint->status = OMX_ENDPOINT_STATUS_CLOSING;
	spin_unlock_irq(&endpoint->status_lock);
	if ((ret = omx_xen_endpoint_release_resources(endpoint, req)) < 0) {
		printk_err("Something went wrong with "
			    "releasing endpoint resources, ret = %d\n", ret);
		goto out;
	}


	omx_wakeup_endpoint_on_close(endpoint);
	omx_iface_detach_endpoint(endpoint, 0);	/*ifacelocked */
	//kref_put(&endpoint->refcount, __omx_xen_endpoint_last_release);
	__omx_xen_endpoint_last_release(&endpoint->refcount);

	spin_lock_irq(&endpoint->status_lock);
	endpoint->status = OMX_ENDPOINT_STATUS_FREE;
	spin_unlock_irq(&endpoint->status_lock);

	ret = 0;

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
