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

extern timers_t t1,t2,t3,t4,t5,t6,t7;

static void omx_xen_timers_reset(void)
{
	//omx_xen_timer_reset(&t_recv);
	//omx_xen_timer_reset(&t_notify);
	//omx_xen_timer_reset(&t_connect);
	//omx_xen_timer_reset(&t_truc);
	omx_xen_timer_reset(&t_send_rndv);
	//omx_xen_timer_reset(&t_tiny);
	//omx_xen_timer_reset(&t_small);
	//omx_xen_timer_reset(&t_medium);
	omx_xen_timer_reset(&t_pull);
	//omx_xen_timer_reset(&t_pull_request);
	//omx_xen_timer_reset(&t_pull_reply);
	omx_xen_timer_reset(&t_send_tiny);
	omx_xen_timer_reset(&t_send_small);
	omx_xen_timer_reset(&t_send_mediumva);
	omx_xen_timer_reset(&t_send_mediumsq_frag);
	omx_xen_timer_reset(&t_send_connect_request);
	omx_xen_timer_reset(&t_send_notify);
	omx_xen_timer_reset(&t_send_connect_reply);
	omx_xen_timer_reset(&t_send_rndv);
	omx_xen_timer_reset(&t_send_liback);
        omx_xen_timer_reset(&t_create_reg);
        omx_xen_timer_reset(&t_destroy_reg);
        omx_xen_timer_reset(&t_reg_seg);
        omx_xen_timer_reset(&t_dereg_seg);
        omx_xen_timer_reset(&t_poke_dom0);

	omx_xen_timer_reset(&t_recv_tiny);
	omx_xen_timer_reset(&t_recv_medsmall);
	omx_xen_timer_reset(&t_recv_mediumsq);
	omx_xen_timer_reset(&t_recv_connect_request);
	omx_xen_timer_reset(&t_recv_connect_reply);
	omx_xen_timer_reset(&t_recv_notify);
	omx_xen_timer_reset(&t_recv_rndv);
	omx_xen_timer_reset(&t_recv_liback);
	omx_xen_timer_reset(&t_pull_done);
	omx_xen_timer_reset(&t_pull_request);

}

static void printk_timer(timers_t * timer, char *name)
{
	if (TIMER_COUNT(timer)) {
		dprintk_inf("%s=%llu count=%lu total_usecs=%llu usec=%llu\n",
			    name, TIMER_TOTAL(timer), TIMER_COUNT(timer),
			    TICKS_TO_USEC(TIMER_TOTAL(timer)),
			    TICKS_TO_USEC(TIMER_TOTAL(timer) /
					  TIMER_COUNT(timer)));
	}
}

static void printk_timers(void)
{
	printk_timer(&t_pull, var_name(t_pull));
	printk_timer(&t_send_tiny, var_name(t_send_tiny));
	printk_timer(&t_send_small, var_name(t_send_small));
	printk_timer(&t_send_mediumva, var_name(t_send_mediumva));
	printk_timer(&t_send_mediumsq_frag, var_name(t_send_mediumsq_frag));
	printk_timer(&t_send_connect_request, var_name(t_send_connect_request));
	printk_timer(&t_send_connect_reply, var_name(t_send_connect_reply));
	printk_timer(&t_send_notify, var_name(t_send_notify));
	printk_timer(&t_send_rndv, var_name(t_send_rndv));
	printk_timer(&t_send_liback, var_name(t_send_liback));
        printk_timer(&t_create_reg, var_name(t_create_reg));
        printk_timer(&t_destroy_reg, var_name(t_destroy_reg));
        printk_timer(&t_poke_dom0, var_name(t_poke_dom0));

	printk_timer(&t_recv_tiny, var_name(t_recv_tiny));
	printk_timer(&t_recv_medsmall, var_name(t_recv_medsmall));
	printk_timer(&t_recv_mediumsq, var_name(t_recv_mediumsq));
	printk_timer(&t_recv_connect_request, var_name(t_recv_connect_request));
	printk_timer(&t_recv_connect_reply, var_name(t_recv_connect_reply));
	printk_timer(&t_recv_notify, var_name(t_recv_notify));
	printk_timer(&t_recv_rndv, var_name(t_recv_rndv));
	printk_timer(&t_recv_liback, var_name(t_recv_liback));
	printk_timer(&t_pull_done, var_name(t_pull_done));
	printk_timer(&t_pull_request, var_name(t_pull_request));

}



/* Grant send/recv queue space as long as the endpoint itself
 * FIXME: Lots of local vars, needs major cleanup! */
int omx_xen_endpoint_grant_resources(struct omx_endpoint *endpoint)
{
	int ret = 0, i = 0;
	grant_ref_t *egref_sendq_list;
	grant_ref_t *egref_recvq_list;
	uint32_t sendq_gref_size = OMX_SENDQ_SIZE / PAGE_SIZE;
	uint32_t recvq_gref_size = OMX_RECVQ_SIZE / PAGE_SIZE;
	grant_ref_t recvq_list, sendq_list, endpoint_gref;
	uint16_t sendq_list_offset, recvq_list_offset, endpoint_offset;
	struct page *sendq_page, *recvq_page, *endpoint_page;
	unsigned long sendq_mfn, recvq_mfn, endpoint_mfn;

	dprintk_in();

	egref_sendq_list =
	    kmalloc(sendq_gref_size * sizeof(grant_ref_t), GFP_KERNEL);
	if (!egref_sendq_list) {
		printk(KERN_ERR "failed to allocate gref_list for sendq\n");
		ret = -ENOMEM;
		goto out;
	}
	egref_recvq_list =
	    kmalloc(recvq_gref_size * sizeof(grant_ref_t), GFP_KERNEL);
	if (!egref_recvq_list) {
		printk(KERN_ERR "failed to allocate gref_list for recvq \n");
		ret = -ENOMEM;
		goto out;
	}

	endpoint->egref_sendq_list = egref_sendq_list;
	endpoint->egref_recvq_list = egref_recvq_list;

	endpoint->sendq_gref_size = sendq_gref_size;
	endpoint->recvq_gref_size = recvq_gref_size;
	dprintk_deb("sendq_gref_size=%#x, recvq_gref_size = %#x\n",
		    sendq_gref_size, recvq_gref_size);

	sendq_list_offset = (unsigned long)egref_sendq_list & ~PAGE_MASK;
	recvq_list_offset = (unsigned long)egref_recvq_list & ~PAGE_MASK;

	endpoint->egref_sendq_offset = sendq_list_offset;
	endpoint->egref_recvq_offset = recvq_list_offset;

	/* FIXME: sendq, recvq + 2 for the pages that host the relevant lists, and one more for the endpoint ;-) */
	ret =
	    gnttab_alloc_grant_references(sendq_gref_size + recvq_gref_size + 3,
					  &endpoint->gref_head);
	if (ret) {
		printk_err
		    ("Cannot allocate %d grant references for sendq/recvq lists\n",
		     sendq_gref_size + recvq_gref_size + 2);
		goto out;
	}

	/* SendQ */
	sendq_page = virt_to_page(egref_sendq_list);
	sendq_mfn = pfn_to_mfn(page_to_pfn(sendq_page));
	sendq_list = gnttab_claim_grant_reference(&endpoint->gref_head);
	gnttab_grant_foreign_access_ref(sendq_list, 0, sendq_mfn, 0);

	endpoint->sendq_gref = sendq_list;

	dprintk_deb("sendq: page=%#x, mfn=%#x, gref=%#x\n", sendq_page,
		    sendq_mfn, endpoint->sendq_gref);

	/* RecvQ*/
	recvq_page = virt_to_page(egref_recvq_list);
	recvq_mfn = pfn_to_mfn(page_to_pfn(recvq_page));
	recvq_list = gnttab_claim_grant_reference(&endpoint->gref_head);
	gnttab_grant_foreign_access_ref(recvq_list, 0, recvq_mfn, 0);

	endpoint->recvq_gref = recvq_list;

	dprintk_deb("recvq: page=%#x, mfn=%#x, gref=%#x\n", recvq_page,
		    recvq_mfn, endpoint->recvq_gref);

	/* Endpoint structure */
	endpoint_page = virt_to_page(endpoint);
	endpoint_offset = (unsigned long)endpoint & ~PAGE_MASK;
	endpoint_mfn = pfn_to_mfn(page_to_pfn(endpoint_page));
	endpoint_gref = gnttab_claim_grant_reference(&endpoint->gref_head);
	gnttab_grant_foreign_access_ref(endpoint_gref, 0, endpoint_mfn, 0);

	endpoint->endpoint_page = endpoint_page;
	endpoint->endpoint_offset = endpoint_offset;
	endpoint->endpoint_mfn = endpoint_mfn;
	endpoint->endpoint_gref = endpoint_gref;

	dprintk_deb("recvq: page=%#x, mfn=%#x, gref=%#x\n", endpoint_page,
		    endpoint_mfn, endpoint->endpoint_gref);

	for (i = 0; i < sendq_gref_size; i++) {
		unsigned long mfn;
		struct page *page = endpoint->sendq_pages[i];
		grant_ref_t gref;
		if (!page) {
			printk_err("sendq: PAGE IS NULL!!!!!\n");
			ret = -EINVAL;
			goto out;
		}
		gref = gnttab_claim_grant_reference(&endpoint->gref_head);
		mfn = pfn_to_mfn(page_to_pfn(page));
		gnttab_grant_foreign_access_ref(gref, 0, mfn, 0);
		egref_sendq_list[i] = gref;
	}

	for (i = 0; i < recvq_gref_size; i++) {
		unsigned long mfn;
		struct page *page = endpoint->recvq_pages[i];
		grant_ref_t gref;
		if (!page) {
			printk_err("recvq: PAGE IS NULL!!!!!\n");
			ret = -EINVAL;
			goto out;
		}
		gref = gnttab_claim_grant_reference(&endpoint->gref_head);
		mfn = pfn_to_mfn(page_to_pfn(page));
		gnttab_grant_foreign_access_ref(gref, 0, mfn, 0);
		egref_recvq_list[i] = gref;
	}

out:
	dprintk_out();
	return ret;
}

/* Release grants for send/recv queue space as long as the endpoint itself
 * FIXME: Lots of local vars, needs major cleanup! */
int omx_xen_endpoint_ungrant_resources(struct omx_endpoint *endpoint)
{
	int ret = 0, i;
	unsigned long sendq_mfn, recvq_mfn, endpoint_mfn;

	dprintk_in();

	/* end all sendq/recvq foreign accesses and release the relevant grants */
	for (i = 0; i < endpoint->sendq_gref_size; i++) {
		struct page *single_page;
		unsigned long mfn;

		single_page = endpoint->sendq_pages[i];
		mfn = pfn_to_mfn(page_to_pfn(single_page));

		ret =
		    gnttab_query_foreign_access(endpoint->egref_sendq_list[i]);
		/* FIXME: Do we need to fail if something goes wrong here ? */
		if (ret) {
			printk_inf
			    ("gref_list[%d] = %u, mfn=%#lx is still in use by the backend!\n",
			     i, endpoint->egref_sendq_list[i], mfn);
		}
		ret =
		    gnttab_end_foreign_access_ref(endpoint->egref_sendq_list[i],
						  0);
		if (!ret)
			printk_inf
			    ("Can't end foreign access for gref_list[%d] = %u, mfn=%#lx\n",
			     i, endpoint->egref_sendq_list[i], mfn);
		gnttab_release_grant_reference(&endpoint->gref_head,
					       endpoint->egref_sendq_list[i]);
	}

	for (i = 0; i < endpoint->recvq_gref_size; i++) {
		struct page *single_page;
		unsigned long mfn;

		single_page = endpoint->recvq_pages[i];
		mfn = pfn_to_mfn(page_to_pfn(single_page));

		ret =
		    gnttab_query_foreign_access(endpoint->egref_recvq_list[i]);
		/* FIXME: Do we need to fail if something goes wrong here ? */
		if (ret)
			printk_inf
			    ("gref_list[%d] = %u, mfn=%#lx is still in use by the backend!\n",
			     i, endpoint->egref_recvq_list[i], mfn);
		ret =
		    gnttab_end_foreign_access_ref(endpoint->egref_recvq_list[i],
						  0);
		if (!ret)
			printk_inf
			    ("Can't end foreign access for gref_list[%d] = %u, mfn=%#lx\n",
			     i, endpoint->egref_recvq_list[i], mfn);
		gnttab_release_grant_reference(&endpoint->gref_head,
					       endpoint->egref_recvq_list[i]);
	}

	/* Release sendq gref_list page grant */
	sendq_mfn = virt_to_mfn(endpoint->egref_sendq_list);
	ret = gnttab_query_foreign_access(endpoint->sendq_gref);
	/* FIXME: Do we need to fail if something goes wrong here ? */
	if (ret) {
		printk_inf
		    ("sendq_gref= %u, mfn=%#lx is still in use by the backend!\n",
		     endpoint->sendq_gref, sendq_mfn);
	}
	ret = gnttab_end_foreign_access_ref(endpoint->sendq_gref, 0);
	if (!ret) {
		printk_inf
		    ("Can't end foreign access for sendq_gref = %u, mfn=%#lx\n",
		     endpoint->sendq_gref, sendq_mfn);
	}

	gnttab_release_grant_reference(&endpoint->gref_head,
				       endpoint->sendq_gref);

	/* Release sendq gref_list page grant */
	recvq_mfn = virt_to_mfn(endpoint->egref_recvq_list);
	ret = gnttab_query_foreign_access(endpoint->recvq_gref);
	/* FIXME: Do we need to fail if something goes wrong here ? */
	if (ret) {
		printk_inf
		    ("recvq_gref= %u, mfn=%#lx is still in use by the backend!\n",
		     endpoint->recvq_gref, recvq_mfn);
	}
	ret = gnttab_end_foreign_access_ref(endpoint->recvq_gref, 0);
	if (!ret) {
		printk_inf
		    ("Can't end foreign access for recvq_gref = %u, mfn=%#lx\n",
		     endpoint->recvq_gref, recvq_mfn);
	}

	/* Release endpoint page grant */
	endpoint_mfn = virt_to_mfn(endpoint);
	/* Extra check, just to be sure nothing gets corrupt */
	if (endpoint_mfn != endpoint->endpoint_mfn) {
		printk_err("mfns do not match!!! will probably fail!\n");
	}

	ret = gnttab_query_foreign_access(endpoint->endpoint_gref);
	/* FIXME: Do we need to fail if something goes wrong here ? */
	if (ret) {
		printk_inf
		    ("endpoint_gref= %u, mfn=%#lx is still in use by the backend!\n",
		     endpoint->endpoint_gref, endpoint->endpoint_mfn);
	}
	gnttab_end_foreign_access_ref(endpoint->endpoint_gref, 0);

	gnttab_release_grant_reference(&endpoint->gref_head,
				       endpoint->recvq_gref);

	gnttab_free_grant_references(endpoint->gref_head);

	kfree(endpoint->egref_sendq_list);
	kfree(endpoint->egref_recvq_list);

	dprintk_out();
	return ret;
}

/* Main Xen2MX endpoint open IOCTL */
int
omx_ioctl_xen_open_endpoint(struct omx_endpoint *endpoint, void __user * uparam)
{
	struct omx_cmd_open_endpoint param;
	struct omx_xenif_request *ring_req;
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	int ret;

	dprintk_in();

	/* Its actually simulating the standard endpoint_open IOCTL */
	ret = copy_from_user(&param, uparam, sizeof(param));
	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		printk(KERN_ERR
		       "Open-MX: Failed to read open endpoint command argument, error %d\n",
		       ret);
		goto out;
	}

	/* test whether the endpoint is ok to be opened
	 * and mark it as initializing */

	spin_lock(&endpoint->status_lock);
	ret = -EBUSY;
	if (endpoint->status != OMX_ENDPOINT_STATUS_FREE) {
		printk_err("status is %d\n", endpoint->status);
		spin_unlock(&endpoint->status_lock);
		goto out;
	}
	/* FIXME: Somewhere around here MPI gets stuck. Need to find a
	 * way to overcome this ASAP! */
	endpoint->status = OMX_ENDPOINT_STATUS_INITIALIZING;
	spin_unlock(&endpoint->status_lock);

	/* alloc internal fields */
	ret = omx_xen_endpoint_alloc_resources(endpoint);
	if (ret < 0)
		goto out_with_init;

	/* grant necessary stuff to the backend (recvq, sendq,
	 * and the endpoint structure itself needed for internal
	 * event queue indices) */
	ret = omx_xen_endpoint_grant_resources(endpoint);
	if (ret < 0)
		goto out_with_alloc;

	endpoint->board_index = param.board_index;
	endpoint->endpoint_index = param.endpoint_index;

	/* Prepare the message to the backend */

	/* FIXME: maybe create a static inline function for this stuff ? */
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_XEN_OPEN_ENDPOINT;
	ring_req->data.endpoint.board_index = param.board_index;
	ring_req->data.endpoint.endpoint_index = param.endpoint_index;
	ring_req->data.endpoint.endpoint = endpoint;
	ring_req->data.endpoint.session_id = endpoint->session_id;
	ring_req->data.endpoint.sendq_gref = endpoint->sendq_gref;
	ring_req->data.endpoint.recvq_gref = endpoint->recvq_gref;
	ring_req->data.endpoint.egref_sendq_offset =
	    endpoint->egref_sendq_offset;
	ring_req->data.endpoint.egref_recvq_offset =
	    endpoint->egref_recvq_offset;
	ring_req->data.endpoint.sendq_gref_size = endpoint->sendq_gref_size;
	ring_req->data.endpoint.recvq_gref_size = endpoint->recvq_gref_size;
	ring_req->data.endpoint.endpoint_gref = endpoint->endpoint_gref;
	ring_req->data.endpoint.endpoint_offset = endpoint->endpoint_offset;

	fe->endpoints[param.endpoint_index] = endpoint;

	endpoint->xen = 1;

	dump_xen_ring_msg_endpoint(&ring_req->data.endpoint);

	omx_poke_dom0(endpoint->fe, ring_req);
	/* FIXME: find a better way to get notified that a backend response has come */
	if (wait_for_backend_response
	    (&endpoint->status, OMX_ENDPOINT_STATUS_INITIALIZING,
	     &endpoint->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
	if (endpoint->status != OMX_ENDPOINT_STATUS_OK) {
		printk_err("Endpoint is busy\n");
		ret = -EBUSY;
		goto out_with_alloc;
	}

	omx_xen_timer_reset(&endpoint->oneway);
	omx_xen_timer_reset(&endpoint->otherway);

	ret = 0;
	goto out;

out_with_alloc:
	printk_err("Out with alloc\n");
	omx_xen_endpoint_free_resources(endpoint);
out_with_init:
	printk_err("Out with init\n");
	spin_lock(&endpoint->status_lock);
	endpoint->status = OMX_ENDPOINT_STATUS_FREE;
	spin_unlock(&endpoint->status_lock);

out:
	omx_xen_timers_reset();
	dprintk_out();
	return ret;

}

/* FIXME: We need our own close_endpoint wrappers, due to
 * iface cleanup/detaching that crashes the frontend! */
void omx_xen_endpoint_free_resources(struct omx_endpoint *endpoint)
{
	might_sleep();
	dprintk_in();

	omx_endpoint_user_regions_exit(endpoint);

	kfree(endpoint->recvq_pages);
	kfree(endpoint->sendq_pages);
	vfree(endpoint->unexp_eventq);
	vfree(endpoint->exp_eventq);
	vfree(endpoint->recvq);
	vfree(endpoint->sendq);
	vfree(endpoint->userdesc);

#ifdef OMX_HAVE_DMA_ENGINE
	omx_dmaengine_put();
#endif
	dprintk_out();
}

static void __omx_xen_endpoint_last_release(struct kref *kref)
{
	struct omx_endpoint *endpoint =
	    container_of(kref, struct omx_endpoint, refcount);
	struct omx_iface *iface = endpoint->iface;

	endpoint->iface = NULL;
	omx_xen_endpoint_free_resources(endpoint);
	kfree(endpoint);
}

int
omx_ioctl_xen_close_endpoint(struct omx_endpoint *endpoint,
			     void __user * uparam)
{
	int ret = 0;
	struct omx_cmd_open_endpoint param;
	struct omx_xenif_request *ring_req;
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	dprintk_in();

	might_sleep();
	if (uparam) {
		ret = copy_from_user(&param, uparam, sizeof(param));
		if (unlikely(ret != 0)) {
			ret = -EFAULT;
			printk(KERN_ERR
			       "Open-MX: Failed to read close endpoint command argument, error %d\n",
			       ret);
			goto out;
		}
	} else {
		param.board_index = endpoint->board_index;
		param.endpoint_index = endpoint->endpoint_index;
	}

	spin_lock(&endpoint->status_lock);

	/* test whether the endpoint is ok to be closed */

	if (endpoint->status == OMX_ENDPOINT_STATUS_FREE) {
		/* not open, just free the structure */
		printk_err("Endpoint already free:S\n");
		spin_unlock(&endpoint->status_lock);
		kfree(endpoint);
		/* FIXME: maybe this is where MPI gets stuck! */
		ret = 0;
		goto out;
	}

	spin_unlock(&endpoint->status_lock);

	/* Prepare the message to the backend */

	/* FIXME: maybe create a static inline function for this stuff ? */
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_XEN_CLOSE_ENDPOINT;
	ring_req->data.endpoint.board_index = param.board_index;
	ring_req->data.endpoint.endpoint_index = param.endpoint_index;
	ring_req->data.endpoint.endpoint = endpoint;
	ring_req->data.endpoint.sendq_gref = endpoint->sendq_gref;
	ring_req->data.endpoint.recvq_gref = endpoint->recvq_gref;
	ring_req->data.endpoint.egref_sendq_offset =
	    endpoint->egref_sendq_offset;
	ring_req->data.endpoint.egref_recvq_offset =
	    endpoint->egref_recvq_offset;
	ring_req->data.endpoint.sendq_gref_size = endpoint->sendq_gref_size;
	ring_req->data.endpoint.recvq_gref_size = endpoint->recvq_gref_size;
	fe->endpoints[param.endpoint_index] = endpoint;
	//dump_xen_ring_msg_endpoint(&ring_req->data.endpoint);
	omx_poke_dom0(endpoint->fe, ring_req);

	/* FIXME: find a better way to get notified that a backend response has come */
	if (wait_for_backend_response
	    (&endpoint->status, OMX_ENDPOINT_STATUS_CLOSING,
	     &endpoint->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	/* FIXME: maybe this is where MPI gets stuck! we don't call these functions! */
	//omx_xen_endpoint_free_resources(endpoint);
	//omx_endpoint_close(endpoint, 0);

	omx_xen_endpoint_ungrant_resources(endpoint);
	fe->endpoints[param.endpoint_index] = NULL;
	/* Just trying! */
	kref_put(&endpoint->refcount, __omx_xen_endpoint_last_release);
	ret = 0;
	goto out;

out:
	printk_timers();
	printk_timer(&endpoint->oneway, var_name(endpoint->oneway));
	printk_timer(&endpoint->otherway, var_name(endpoint->otherway));
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
