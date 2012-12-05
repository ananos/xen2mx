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

#ifndef __omx_xenback_helper_h__
#define __omx_xenback_helper_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_endpoint.h"

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_event.h"

static int map_frontend_page(omx_xenif_t * omx_xenif, struct vm_struct *vm_area,
			     grant_handle_t * handle, grant_ref_t * gref)
{
	struct gnttab_map_grant_ref op;
	int ret = 0;

	dprintk_in();
	if (!gref) {
		printk_err("Are you fucking kidding me ? gref = NULL\n");
		ret = -EINVAL;
		goto out;
	}
	gnttab_set_map_op(&op,
			  (unsigned long)vm_area->addr,
			  GNTMAP_host_map, *gref, omx_xenif->domid);

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1))
		BUG();

	if (op.status) {
		printk_err("Grant table operation failure !\n");
		ret = op.status;
		goto out;
	}

	*handle = op.handle;

out:
	dprintk_out();
	return ret;
}

static void unmap_frontend_page(omx_xenif_t * omx_xenif, struct vm_struct *area,
				grant_handle_t handle)
{
	struct gnttab_unmap_grant_ref op;

	dprintk_in();
	gnttab_set_unmap_op(&op,
			    (unsigned long)area->addr, GNTMAP_host_map, handle);

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1))
		BUG();
	if (op.status) {
		printk_err("unmap failed\n");
	}
	dprintk_out();
}

static void backend_create_omx(struct backend_info *be)
{
	int err = 0;
	unsigned handle;
	struct xenbus_device *dev = be->dev;
	struct omxback_dev *omxdev;
	int i = 0;

	dprintk_in();

	if (be->omxdev != NULL) {
		dprintk_deb("already malloced, no worries though!\n");
		return;
	}

	dprintk_deb("Will read handle, and malloc OMXDEV\n");
	err = xenbus_scanf(XBT_NIL, dev->nodename, "handle", "%u", &handle);
	dprintk_deb("handle = %u, err = %d\n", handle, err);
	if (err != 1) {
		//handle=2;
		xenbus_dev_fatal(dev, err, "reading handle");
	}

	be->omxdev = kzalloc(sizeof(struct omxback_dev), GFP_KERNEL);
	omxdev = be->omxdev;
	for (i = 0; i < OMX_XEN_MAX_ENDPOINTS; i++) {
		struct omx_endpoint *e;
		e = kzalloc(sizeof(struct omx_endpoint), GFP_KERNEL);
		omxdev->endpoints[i] = e;
		kref_init(&e->refcount);
		dprintk_deb("omxdev(%#llx)->endpoints(%#llx)[%d] = %#llx!\n",
			    (unsigned long long)omxdev,
			    (unsigned long long)omxdev->endpoints[i], i,
			    (unsigned long long)e);
		dprintk_deb
		    ("attached endpoint to omxback_dev!, e->refcount=%d\n",
		     atomic_read(&e->refcount.refcount));
		spin_lock_init(&e->status_lock);
		e->status = OMX_ENDPOINT_STATUS_FREE;
		e->xen = 1;
		e->be = be;
	}
	kobject_uevent(&dev->dev.kobj, KOBJ_ONLINE);
	dprintk_out();
}

int omx_xenif_map(omx_xenif_t * omx_xenif, struct vm_struct **ring_area,
		  struct omx_xenif_back_ring *ring, grant_ref_t * gref,
		  grant_handle_t * handle)
{
	int err = 0;
	struct omx_xenif_sring *sring;
	struct vm_struct *area;

	dprintk_in();

	/* Already connected through? */
	if (omx_xenif->irq)
		goto out;

	if (!handle || !gref) {
		printk_err("wrong handle, grefs\n");
		err = -EINVAL;
		goto out;
	}

	area = alloc_vm_area(PAGE_SIZE, NULL);
	if (!area) {
		err = -ENOMEM;
		goto out;
	}
	*ring_area = area;

	err = map_frontend_page(omx_xenif, *ring_area, handle, gref);
	if (err < 0) {
		unmap_frontend_page(omx_xenif, *ring_area, *handle);
		free_vm_area(area);
		ring->sring = NULL;
		printk_err("failed to bind event channel to irqhandler"
			   "err=%d\n", err);
		goto out;
	}

	sring = (struct omx_xenif_sring *)area->addr;
	BACK_RING_INIT(ring, sring, PAGE_SIZE);

out:
	dprintk_out();
	return err;
}

void omx_xenif_disconnect(omx_xenif_t * omx_xenif)
{

	dprintk_in();
	//atomic_dec(&omx_xenif->refcnt);
	//wait_event(omx_xenif->waiting_to_free, atomic_read(&omx_xenif->refcnt) == 0);
	//atomic_inc(&omx_xenif->refcnt);

	dprintk_deb("%s: rspvt = %d, rc = %d, rp = %d\n", __func__,
		    omx_xenif->ring.rsp_prod_pvt, omx_xenif->ring.req_cons,
		    omx_xenif->ring.sring->req_prod);
	if (omx_xenif->irq) {
		unbind_from_irqhandler(omx_xenif->irq, omx_xenif);
		omx_xenif->irq = 0;
	}

	if (omx_xenif->ring.sring) {
		unmap_frontend_page(omx_xenif, omx_xenif->omx_xenif_ring_area,
				    omx_xenif->shmem_handle);
		free_vm_area(omx_xenif->omx_xenif_ring_area);
		omx_xenif->ring.sring = NULL;
	}
	destroy_workqueue(omx_xenif->msg_workq);
	if (omx_xenif->recv_ring.sring) {
		unmap_frontend_page(omx_xenif, omx_xenif->recv_ring_area,
				    omx_xenif->recv_handle);
		free_vm_area(omx_xenif->recv_ring_area);
		omx_xenif->recv_ring.sring = NULL;
	}
	destroy_workqueue(omx_xenif->response_msg_workq);
#ifdef OMX_XEN_COOKIES
		omx_xen_page_free_cookies(omx_xenif);
#endif
	kfree(omx_xenif);
	dprintk_out();
}

void omx_xenif_free(omx_xenif_t * omx_xenif)
{
	dprintk_in();
	if (!atomic_dec_and_test(&omx_xenif->refcnt))
		BUG();
	//destroy_workqueue(omx_xenif->msg_workq);
	//kfree(omx_xenif);
	dprintk_out();
}

static int connect_ring(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	omx_xenif_t *omx_xenif = be->omx_xenif;
	unsigned int evtchn;
	int err;
	char omx_xenif_backend_name[20];

	dprintk_in();

	err =
	    xenbus_gather(XBT_NIL, dev->otherend, "ring-ref", "%lu",
			  &omx_xenif->shmem_ref, "event-channel", "%u", &evtchn,
			  "recv-ring-ref", "%lu", &omx_xenif->recv_ref, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/ring-ref and event-channel",
				 dev->otherend);
		goto out;
	}

	dprintk_deb("ring-ref %ld, event-channel %d, recv_ring_ref %lu\n",
		    omx_xenif->shmem_ref, evtchn, omx_xenif->recv_ref);

	/* Map the shared frame */
	err =
	    omx_xenif_map(omx_xenif, &omx_xenif->omx_xenif_ring_area,
			  &omx_xenif->ring, &omx_xenif->shmem_ref,
			  &omx_xenif->shmem_handle);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref %#x port %#x",
				 omx_xenif->shmem_ref, evtchn);
		printk_err("Unable to map ring-ref (%#x) and port (%#x), %d\n",
			   omx_xenif->shmem_ref, evtchn, err);
		goto out;
	}

	/* Map the shared frame */
	err =
	    omx_xenif_map(omx_xenif, &omx_xenif->recv_ring_area,
			  &omx_xenif->recv_ring, &omx_xenif->recv_ref,
			  &omx_xenif->recv_handle);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping recv_ring-ref %#x port %#x",
				 omx_xenif->recv_ref, evtchn);
		printk_err("Unable to map ring-ref (%#x) and port (%#x), %d\n",
			   omx_xenif->recv_ref, evtchn, err);
		goto out;
	}

	/* end grant */
	dprintk_inf("Will bind otherend_id = %u port = %#lx\n",
		    dev->otherend_id, (unsigned long)be->evtchn.port);
	sprintf(omx_xenif_backend_name, "xenifbe%x_%lu",
		omx_xenif->shmem_handle, (unsigned long)be->evtchn.port);

	err =
	    bind_evtchn_to_irqhandler(be->evtchn.port, omx_xenif_be_int,
				      IRQF_SHARED,
				      omx_xenif_backend_name, omx_xenif);
	be->irq = err;
	if (err < 0) {
		printk_err("failed binding evtchn to irqhandler!, err = %d\n",
			   err);
		goto out;
	}

#ifdef OMX_XEN_COOKIES
	INIT_LIST_HEAD(&omx_xenif->page_cookies_free);
	rwlock_init(&omx_xenif->page_cookies_freelock);
	INIT_LIST_HEAD(&omx_xenif->page_cookies_inuse);
	rwlock_init(&omx_xenif->page_cookies_inuselock);
#endif

	err = xenbus_switch_state(dev, XenbusStateConnected);
	backend_create_omx(be);

	dprintk_out();
	return 0;
out:
	dprintk_out();
	return err;
}

omx_xenif_t *omx_xenif_alloc(domid_t domid)
{
	omx_xenif_t *omx_xenif;
	int err;
	char omx_xenback_workqueue_name[20];
	char omx_xenback_workqueue_name_2[20];

	dprintk_in();
	omx_xenif = kzalloc(sizeof(omx_xenif_t), GFP_KERNEL);
	if (!omx_xenif)
		return ERR_PTR(-ENOMEM);

	dprintk_deb("omx_xenif is @ %#llx\n", (unsigned long long)omx_xenif);
	omx_xenif->domid = domid;
	sprintf(omx_xenback_workqueue_name, "ReqWQ-%d", domid);
	sprintf(omx_xenback_workqueue_name_2, "RespWQ-%d", domid);
	spin_lock_init(&omx_xenif->omx_resp_lock);
	spin_lock_init(&omx_xenif->omx_ring_lock);
	spin_lock_init(&omx_xenif->omx_be_lock);
	init_waitqueue_head(&omx_xenif->wq);
	atomic_set(&omx_xenif->refcnt, 1);
	init_waitqueue_head(&omx_xenif->waiting_to_free);
	omx_xenif->msg_workq =
	    create_singlethread_workqueue(omx_xenback_workqueue_name);
	if (unlikely(!omx_xenif->msg_workq)) {
		printk_err("Couldn't create msg_workq!\n");
		err = -ENOMEM;
		return (void *)0;
	}

	INIT_WORK(&omx_xenif->msg_workq_task, msg_workq_handler);

	omx_xenif->response_msg_workq =
	    create_singlethread_workqueue(omx_xenback_workqueue_name_2);
	if (unlikely(!omx_xenif->response_msg_workq)) {
		printk_err("Couldn't create msg_workq!\n");
		err = -ENOMEM;
		return (void *)0;
	}

	INIT_WORK(&omx_xenif->response_workq_task, response_workq_handler);

	dprintk_out();
	return omx_xenif;
}

static int omx_xenback_allocate_basic_structures(struct xenbus_device *dev, const struct xenbus_device_id
						 *id)
{
	struct backend_info *be;
	int ret = 0;

	dprintk_in();

	be = kzalloc(sizeof(struct backend_info), GFP_KERNEL);
	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating backend structure");
		goto out;
	}
	dprintk_deb("Backend structure is @%#lx\n", (unsigned long)be);

	be->dev = dev;
	dev_set_drvdata(&dev->dev, be);
	be->omx_xenif = omx_xenif_alloc(dev->otherend_id);
	spin_lock_init(&be->lock);
	if (IS_ERR(be->omx_xenif)) {
		ret = PTR_ERR(be->omx_xenif);
		be->omx_xenif = NULL;
		xenbus_dev_fatal(dev, ret, "creating omx Xen interface");
		goto out;
	}
	dprintk_deb("OMX xen Interface is @%#lx\n",
		    (unsigned long)be->omx_xenif);
out:
	dprintk_out();
	return ret;
}

static int omx_xenback_setup_evtchn(struct xenbus_device *dev,
				    struct backend_info *be)
{

	int ret = 0;
	dprintk_in();

	be->remoteDomain = dev->otherend_id;
	be->omx_xenif->be = be;
	dprintk_deb("be is @ %#lx\n", (unsigned long)be);
	dprintk_deb("omx_xenif->be is @ %#lx\n",
		    (unsigned long)be->omx_xenif->be);
	be->evtchn.dom = 0;
	be->evtchn.remote_dom = dev->otherend_id;
	ret = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &be->evtchn);
	if (ret) {
		printk_err("Failed to allocate evtchn!\n");
		goto out;
	}
	dprintk_deb("Allocated Event Channel to %d\n", dev->otherend_id);
out:
	dprintk_out();
	return ret;
}

static int omx_xenback_setup_xenbus(struct xenbus_device *dev,
				    struct backend_info *be)
{
	int ret = 0;
	const char *message;
	struct xenbus_transaction xbt;
	dprintk_in();

	do {
		ret = xenbus_transaction_start(&xbt);
		if (ret) {
			xenbus_dev_fatal(dev, ret, "starting transaction");
			goto out;
		}

		ret = xenbus_printf(xbt, dev->otherend, "gref", "%d", 0);
		if (ret) {
			message = "writing gref";
			goto abort_transaction;
		}

		ret =
		    xenbus_printf(xbt, dev->otherend, "port", "%d",
				  be->evtchn.port);
		if (ret) {
			message = "writing port";
			goto abort_transaction;
		}

		ret = xenbus_transaction_end(xbt, 0);
	} while (ret == -EAGAIN);

	dprintk_deb("Wrote port %d to %s/%s\n", be->evtchn.port,
		    dev->nodename, "port");
	if (ret) {
		xenbus_dev_fatal(dev, ret, "completing transaction");
		goto out;
	}

	goto out;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, ret, "%s", message);

out:
	dprintk_out();
	return ret;
}

#endif				/* __omx_xenback_helper_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
