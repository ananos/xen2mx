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
#include <linux/kthread.h>
#include <linux/freezer.h>

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


#define OMX_XEN_POLL_HARD_LIMIT 5000000UL
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xen_lib.h"
#include "omx_xenback_reg.h"
#include "omx_xenback_endpoint.h"
#include "omx_xenback_event.h"


//timers_t t1,t2,t3,t4,t5,t6,t7,t8;

static void omx_xen_timers_reset(void)
{
	omx_xen_timer_reset(&t_recv);
	omx_xen_timer_reset(&t_notify);
	omx_xen_timer_reset(&t_connect);
	omx_xen_timer_reset(&t_truc);
	omx_xen_timer_reset(&t_rndv);
	omx_xen_timer_reset(&t_tiny);
	omx_xen_timer_reset(&t_small);
	omx_xen_timer_reset(&t_medium);
	omx_xen_timer_reset(&t_pull);
	omx_xen_timer_reset(&t_pull_request);
	omx_xen_timer_reset(&t_pull_reply);
	omx_xen_timer_reset(&t_send_tiny);
	omx_xen_timer_reset(&t_send_small);
	omx_xen_timer_reset(&t_send_medium);
	omx_xen_timer_reset(&t_send_connect);
	omx_xen_timer_reset(&t_send_notify);
	omx_xen_timer_reset(&t_send_connect_reply);
	omx_xen_timer_reset(&t_send_rndv);
	omx_xen_timer_reset(&t_send_liback);
	omx_xen_timer_reset(&t_create_reg);
	omx_xen_timer_reset(&t_destroy_reg);
	omx_xen_timer_reset(&t_reg_seg);
	omx_xen_timer_reset(&t_dereg_seg);
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
	printk_timer(&t_recv, var_name(t_recv));
	printk_timer(&t_notify, var_name(t_notify));
	printk_timer(&t_connect, var_name(t_connect));
	printk_timer(&t_truc, var_name(t_truc));
	printk_timer(&t_rndv, var_name(t_rndv));
	printk_timer(&t_tiny, var_name(t_tiny));
	printk_timer(&t_small, var_name(t_small));
	printk_timer(&t_medium, var_name(t_medium));
	printk_timer(&t_pull, var_name(t_pull));
	printk_timer(&t_pull_request, var_name(t_pull_request));
	printk_timer(&t_pull_reply, var_name(t_pull_reply));
	printk_timer(&t_send_tiny, var_name(t_send_tiny));
	printk_timer(&t_send_small, var_name(t_send_small));
	printk_timer(&t_send_medium, var_name(t_send_medium));
	printk_timer(&t_send_connect, var_name(t_send_connect));
	printk_timer(&t_send_notify, var_name(t_send_notify));
	printk_timer(&t_send_connect_reply, var_name(t_send_connect_reply));
	printk_timer(&t_send_rndv, var_name(t_send_rndv));
	printk_timer(&t_send_liback, var_name(t_send_liback));
	printk_timer(&t_create_reg, var_name(t_create_reg));
	printk_timer(&t_destroy_reg, var_name(t_destroy_reg));
	printk_timer(&t_reg_seg, var_name(t_reg_seg));
	printk_timer(&t_dereg_seg, var_name(t_dereg_seg));
}

int omx_xen_process_incoming_response(omx_xenif_t * omx_xenif,
				      struct omx_xenif_back_ring *ring,
				      RING_IDX * cons_idx, RING_IDX * prod_idx);
int omx_xen_process_message(omx_xenif_t * omx_xenif,
			    struct omx_xenif_back_ring *ring);

static int omx_xen_setup_and_send_mediumsq_frag(struct omx_endpoint *endpoint, struct omx_cmd_xen_send_mediumsq_frag
						*cmd)
{
	int ret = 0;
	struct omx_cmd_send_mediumsq_frag *cmd_mediumsq_frag =
	    &cmd->mediumsq_frag;

	dprintk_in();

	ret = omx_ioctl_send_mediumsq_frag(endpoint, cmd_mediumsq_frag);
	if (ret) {
		printk_err("send_mediumsq_frag failed\n");
	}

	dprintk_out();
	return ret;
}

#define MEDIUMVA_FAKE 0
static int omx_xen_setup_and_send_mediumva(struct omx_endpoint *endpoint, struct omx_cmd_xen_send_mediumva
					   *cmd)
{
	int ret = 0, i;
	uint16_t first_page_offset = cmd->first_page_offset;
	struct omx_cmd_send_mediumva *cmd_mediumva = &cmd->mediumva;
#if MEDIUMVA_FAKE
	struct omx_cmd_send_small *cmd_small = &cmd->mediumva;
#endif
	grant_ref_t *grefs = cmd->grefs;
	void *vaddr;
	unsigned long vaddrs[9], flags;
	grant_handle_t handles[9];
#ifdef OMX_XEN_COOKIES
	struct omx_xen_page_cookie *cookies[9];
#endif
	uint8_t nr_pages = 0;
	struct omx_cmd_user_segment *usegs, *cur_useg;
	struct page **pages;
	uint32_t length;
	struct page *page;

	dprintk_in();

	spin_lock_irqsave(&endpoint->be->omx_xenif->omx_send_lock, flags);
	nr_pages = cmd->nr_pages;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		printk_err("pages allocation failed\n");
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < nr_pages; i++) {
		pages[i] = NULL;
		ret =
		    omx_xen_map_page(endpoint->be, grefs[i], &vaddr,
				     &handles[i], &page,
#ifdef OMX_XEN_COOKIES
				     &cookies[i]);
#else
				     NULL);
#endif
		if (ret) {
			printk_err("cannot map page ret = %d\n", ret);
			goto out;
		}
		dprintk_deb("vaddr=%p, handle[%d] = %#x, page=%#x\n", vaddr, i,
			    handles[i], page);
		vaddrs[i] = (unsigned long)vaddr;
		pages[i] = page;
	}

#if 0
	vaddr = vmap(pages, nr_pages, VM_IOREMAP, PAGE_KERNEL);
	if (!vaddr) {
		printk_err("vaddr is NULL\n");
		ret = -EFAULT;
		goto out;
	}
#endif

	usegs =
	    kmalloc(nr_pages * sizeof(struct omx_cmd_user_segment), GFP_KERNEL);
	if (!usegs) {
		printk_err("Cannot malloc usegs!\n");
		ret = -ENOMEM;
		goto out;
	}
/* FIXME: are we doing correct address/page calculations ? Maybe this is the source of corruption */
//      usegs->vaddr = (unsigned long) vaddr; //vaddrs[0] + first_page_offset;
	length = cmd_mediumva->length;
	for (i = 0; i < nr_pages; i++) {
		uint32_t rem = length > PAGE_SIZE ? PAGE_SIZE : length;
		cur_useg = &usegs[i];
		cur_useg->vaddr = vaddrs[i];
		cur_useg->len = rem;
		dprintk_deb("usegs=%#lx, usegs.len = %d, vaddr = %p\n",
			    cur_useg, cur_useg->len, cur_useg->vaddr);

		if (first_page_offset && i == 0) {
			if (length + first_page_offset > PAGE_SIZE) {
				rem -= first_page_offset;
				cur_useg->len = rem;
			}
			cur_useg->vaddr += first_page_offset;
		}
		dprintk_deb
		    ("after_offset usegs=%#lx, usegs.len = %d, vaddr = %p\n",
		     cur_useg, cur_useg->len, cur_useg->vaddr);
		length -= rem;
	}
	cmd_mediumva->segments = (uint64_t) & usegs[0];
	cmd_mediumva->nr_segments = nr_pages;
	spin_unlock_irqrestore(&endpoint->be->omx_xenif->omx_send_lock, flags);

#if MEDIUMVA_FAKE
	{
		uint32_t checksum = cmd_mediumva->seqnum;
		cmd_small->length = 128;
		cmd_small->checksum = checksum;
		cmd_small->vaddr = (uint64_t) cmd;
		ret = omx_ioctl_send_small(endpoint, cmd_small);
	}
#else
	ret = omx_ioctl_send_mediumva(endpoint, cmd_mediumva);
#endif
	if (ret) {
		printk_err("send_mediumva failed\n");
	}
#if 0
	vunmap(vaddr);
#endif
	for (i = 0; i < nr_pages; i++) {
		dprintk_deb("gref[%d] = %#x\n", i, grefs[i]);
		ret = omx_xen_unmap_page(handles[i], pages[i]);
		if (ret) {
			printk_err("cannot unmap page ret = %d\n", ret);
			goto out;
		}
#ifdef OMX_XEN_COOKIES
		omx_xen_page_put_cookie(endpoint->be->omx_xenif, cookies[i]);
#endif
	}

	kfree(pages);

out:
	dprintk_out();
	return ret;
}

/* Function to poke the guest with a filled response.
 * We only use recv_ring, as this is the only ring
 * we can use to notify the guest */
int omx_poke_domU(omx_xenif_t * omx_xenif, struct omx_xenif_response *ring_resp)
{
	int err = 0;
	unsigned long flags;
	struct evtchn_send event;
	int notify;
	struct omx_xenif_back_ring *ring;

	dprintk_in();

	spin_lock_irqsave(&omx_xenif->omx_resp_lock, flags);

	if (unlikely(!ring_resp)) {
		/* If our ring buffer is null, then we fail ungracefully */
		printk_err("Null ring_resp\n");
		err = -EINVAL;
		goto out;
	}
	ring = &omx_xenif->recv_ring;
	dprintk_deb
	    ("Poke domU func = %#x, response_produced_private = %d, "
	     "requests_produced = %d, responses= %d\n",
	     ring_resp->func, ring->rsp_prod_pvt, ring->sring->req_prod,
	     ring->sring->rsp_prod);

	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);
	if (notify) {
		event.port = omx_xenif->be->evtchn.port;
		err = HYPERVISOR_event_channel_op(EVTCHNOP_send, &event);
		if (err) {
			printk_err("Failed to send event, err = %d", err);
			goto out;
		}
	}
out:
	spin_unlock_irqrestore(&omx_xenif->omx_resp_lock, flags);
	dprintk_out();
	return err;
}

/* Our soft interrupt handler */
irqreturn_t omx_xenif_be_int(int irq, void *data)
{
	omx_xenif_t *omx_xenif = (omx_xenif_t *) data;
	//struct backend_info *be = omx_xenif->be;
	unsigned long flags;
	int pending_reqs;

	dprintk_in();
	spin_lock_irqsave(&omx_xenif->omx_be_lock, flags);

	//dprintk_deb("event_ptr=%p info=%#lx\n", data, (unsigned long)be);
	RING_FINAL_CHECK_FOR_REQUESTS(&omx_xenif->ring, pending_reqs);
	if (pending_reqs) {
#ifdef OMX_XENBACK_POLLING
		omx_xenif->waiting_reqs=1;
		wake_up(&omx_xenif->wq);
#else
		queue_work(omx_xenif->msg_workq, &omx_xenif->msg_workq_task);
#endif
		//msg_workq_handler(&omx_xenif->msg_workq_task);
	}

	if (RING_HAS_UNCONSUMED_REQUESTS(&omx_xenif->recv_ring)) {
		/* Since we don't really do anythine else than
		 * keeping a balance on the ring, we just call the
		 * function, without the workqueue */
		//queue_work(omx_xenif->response_msg_workq,
		//	   &omx_xenif->response_workq_task);
		response_workq_handler(&omx_xenif->response_workq_task);
	}

	spin_unlock_irqrestore(&omx_xenif->omx_be_lock, flags);
	dprintk_out();
	return IRQ_HANDLED;
}

/* something like the "bottom half" for responses (recv_ring).
 * We only use it for Xen's perverted idea of balanced
 * requests and responses. We'll have to dig into it a bit
 * more to figure out if we actually need to do this ;-) */
void response_workq_handler(struct work_struct *work)
{
	omx_xenif_t *omx_xenif;
	struct backend_info *be;
	unsigned long flags;
	int more_to_do = 0;
	int ret = 0;
	struct omx_xenif_back_ring *ring;
	int i=0;

	dprintk_in();

	dprintk_deb("%s: started\n", current->comm);

	omx_xenif = container_of(work, omx_xenif_t, response_workq_task);
	spin_lock_irqsave(&omx_xenif->omx_recv_ring_lock, flags);
	if (unlikely(!omx_xenif)) {
		printk_err("Got NULL for omx_xenif, aborting!\n");
		goto out;
	}
	be = omx_xenif->be;
	if (unlikely(!be)) {
		printk_err("Got NULL for be, aborting!\n");
		goto out;
	}

	spin_unlock_irqrestore(&omx_xenif->omx_recv_ring_lock, flags);
again:
	ring = &omx_xenif->recv_ring;
	if (RING_HAS_UNCONSUMED_REQUESTS(ring)) {
		/* FIXME: We have to find a way to properly lock
		 * when calling process_incoming_response */
	//	spin_unlock_irqrestore(&omx_xenif->omx_recv_ring_lock, flags);
		ret =
		    omx_xen_process_incoming_response(omx_xenif, ring,
						      &ring->req_cons,
						      &ring->sring->req_prod);
	//	spin_lock_irqsave(&omx_xenif->omx_recv_ring_lock, flags);
	}

	RING_FINAL_CHECK_FOR_REQUESTS(ring, more_to_do);
	if (more_to_do) {
		goto again;
	}

out:
	//spin_unlock_irqrestore(&omx_xenif->omx_recv_ring_lock, flags);
	dprintk_out();

}

/* polling for requests */
int omx_xenbk_thread(void *data)
{
	omx_xenif_t *omx_xenif;
	struct evtchn_send event;
	struct backend_info *be;
	unsigned long flags;
	int more_to_do = 0;
	int ret = 0;
	struct omx_xenif_back_ring *ring;
	int notify;
	int i = 0;

	dprintk_in();

	dprintk_deb("%s: started\n", current->comm);

	omx_xenif = (omx_xenif_t *) data;
	if (unlikely(!omx_xenif)) {
		printk_err("Got NULL for omx_xenif, aborting!\n");
		goto out;
	}

	while (!kthread_should_stop()) {
		int i = 0;
		if (try_to_freeze())
			continue;
		ring = &omx_xenif->ring;
		if (unlikely(!ring)) {
			RING_FINAL_CHECK_FOR_REQUESTS(ring, more_to_do);
			wait_event_interruptible(omx_xenif->wq, more_to_do
						 || kthread_should_stop());
		} else {
			wait_event_interruptible(omx_xenif->wq, omx_xenif->waiting_reqs
						 || kthread_should_stop());
			omx_xenif->waiting_reqs = 0;
		}
//again:
		do {
			RING_FINAL_CHECK_FOR_REQUESTS(ring, more_to_do);
			if (more_to_do) {
				/* FIXME: We have to find a way to properly lock
				 * when calling process_incoming_response */
				ret = omx_xen_process_message(omx_xenif, ring);

				RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring,
								     notify);
				if (notify) {
					event.port = omx_xenif->be->evtchn.port;
					if (HYPERVISOR_event_channel_op
					    (EVTCHNOP_send, &event) != 0) {
						printk_err
						    ("error sending response\n");
					}
				}
			}
#if 0
			else {
				if (i++ > 10000)
					break;
				cpu_relax();
			}
#endif
		} while (more_to_do);

	}

out:
	dprintk_out();
	return 0;

}
/* something like the "bottom half" for requests (ring) */
void msg_workq_handler(struct work_struct *work)
{
	omx_xenif_t *omx_xenif;
	struct evtchn_send event;
	struct backend_info *be;
	unsigned long flags;
	int more_to_do = 0;
	int ret = 0;
	struct omx_xenif_back_ring *ring;
	int notify;
	int i = 0;

	dprintk_in();

	dprintk_deb("%s: started\n", current->comm);

	omx_xenif = container_of(work, omx_xenif_t, msg_workq_task);
	spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
	if (unlikely(!omx_xenif)) {
		printk_err("Got NULL for omx_xenif, aborting!\n");
		goto out;
	}
	be = omx_xenif->be;
	if (unlikely(!be)) {
		printk_err("Got NULL for be, aborting!\n");
		goto out;
	}

	spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);

again:
	while (1) {
		//spin_lock_irqrestore(&omx_xenif->omx_ring_lock, flags);
		ring = &omx_xenif->ring;
		if (RING_HAS_UNCONSUMED_REQUESTS(ring)) {
			/* FIXME: We have to find a way to properly lock
			 * when calling process_incoming_response */
			//spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);
			ret = omx_xen_process_message(omx_xenif, ring);
			//spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);

			RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);
			if (notify) {
				event.port = omx_xenif->be->evtchn.port;
				if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &event) != 0) {
					printk_err("error sending response\n");
				}
			}
		} else {
			//spin_unlock_irqsave(&omx_xenif->omx_ring_lock, flags);
			i++;
			if (i> 10000) {
				break;
			}
			//cpu_relax();
		}

	}

	RING_FINAL_CHECK_FOR_REQUESTS(ring, more_to_do);
	if (more_to_do) {
		goto again;
	}

out:
	//spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);
	dprintk_out();

}

int omx_xen_process_incoming_response(omx_xenif_t * omx_xenif,
				      struct omx_xenif_back_ring *ring,
				      RING_IDX * cons_idx, RING_IDX * prod_idx)
{
	RING_IDX cons;
	RING_IDX prod;
	struct omx_xenif_request *req;
	struct backend_info *be;
	unsigned long flags;
	int ret = 0;
	uint32_t func;

	dprintk_in();

	if (unlikely(!omx_xenif)) {
		printk_err("Got NULL for omx_xenif, aborting!\n");
		goto out;
	}
	be = omx_xenif->be;
	if (unlikely(!be)) {
		printk_err("Got NULL for be, aborting!\n");
		goto out;
	}
	if (!ring) {
		printk_err("No ring to process\n");
		ret = -EINVAL;
		goto out;
	}

	rmb();

	cons = *cons_idx;
	prod = *prod_idx;

	dprintk_deb("ring=%p, consumed = %d, requests_produced= %d\n", ring,
		    *cons_idx, *prod_idx);
	rmb();
	while (cons != prod) {
		dprintk_deb("req_cons=%d, produced=%d\n", cons, prod);

		spin_lock_irqsave(&omx_xenif->omx_recv_ring_lock, flags);
		req = RING_GET_REQUEST(ring, cons++);
		if (unlikely(!req)) {
			printk_err("Got NULL for req, aborting!\n");
			goto out;
		}
		func = req->func;
		if (unlikely(!func)) {
			printk_err("%s: Got NULL for req->func, aborting!\n",
				   __func__);
			dprintk_deb("req_cons=%d, produced=%d\n", cons, prod);
			goto out;
		}

		switch (func) {
		case OMX_CMD_RECV_CONNECT_REPLY:
		case OMX_CMD_RECV_CONNECT_REQUEST:
		case OMX_CMD_RECV_RNDV:
		case OMX_CMD_RECV_NOTIFY:
		case OMX_CMD_RECV_LIBACK:
		case OMX_CMD_RECV_MEDIUM_FRAG:
		case OMX_CMD_RECV_SMALL:
		case OMX_CMD_RECV_TINY:
		case OMX_CMD_XEN_DUMMY:{
				/* FIXME: Xen's perverted idea of balanced responses/requests */
				break;
			}
		default:{
				printk_err("No usefull command received: %x\n",
					   func);
				ret = -EINVAL;
			}
		}
		spin_unlock_irqrestore(&omx_xenif->omx_recv_ring_lock, flags);
	}

	*cons_idx = cons;
	wmb();
out:
	dprintk_out();
	return ret;
}


static struct omx_endpoint *omx_xenback_get_endpoint(struct backend_info *be, struct omx_xenif_request *req)
{
	uint32_t bi, eid;
	struct omx_endpoint *endpoint = NULL;

	dprintk_in();

	bi = req->board_index;
	eid = req->eid;
	endpoint = be->omxdev->endpoints[eid];

	dprintk_deb("got (%d,%d)\n", bi, eid);

	dprintk_out();
	return endpoint;
}

static void omx_xenback_prepare_response(struct omx_endpoint *endpoint,
					 struct omx_xenif_request *req,
					 struct omx_xenif_response * resp, int ret)
{
	dprintk_in();

	resp->func = req->func;
	resp->eid = endpoint->endpoint_index;
	resp->board_index = endpoint->board_index;
	resp->ret = ret;
	dprintk_out();

}

int omx_xenback_process_misc(omx_xenif_t * omx_xenif, uint32_t func, struct
				 omx_xenif_request *req,
				 struct omx_xenif_response *resp)
{
	struct backend_info *be = omx_xenif->be;
	unsigned long flags;
	int ret = 0;

	dprintk_in();

	switch (func) {
	case OMX_CMD_PEER_FROM_INDEX:
	case OMX_CMD_PEER_FROM_ADDR:
	case OMX_CMD_PEER_FROM_HOSTNAME:{
			struct omx_cmd_misc_peer_info peer_info;
			dprintk_deb
			    ("received frontend request: OMX_CMD_GET_PEER_FROM_%#lx, param=%lx\n",
			     req->func,
			     sizeof(struct omx_cmd_xen_misc_peer_info));

			memcpy(&peer_info, &req->data.mpi.info,
			       sizeof(struct omx_cmd_misc_peer_info));
			memcpy(peer_info.hostname,
			       req->data.mpi.info.hostname,
			       sizeof(req->data.mpi.info.hostname));
			dprintk_deb("peer_info.index = %lx\n",
				    (unsigned long)peer_info.index);
			dprintk_deb("peer_info.board_addr = %#llx\n",
				    peer_info.board_addr);
			dprintk_deb("peer_info.hostname =  %s\n",
				    peer_info.hostname);
			if (func == OMX_CMD_PEER_FROM_INDEX)
				ret =
				    omx_peer_lookup_by_index
				    (peer_info.index,
				     &peer_info.board_addr, peer_info.hostname);
			else if (func == OMX_CMD_PEER_FROM_ADDR)
				ret =
				    omx_peer_lookup_by_addr
				    (peer_info.board_addr,
				     peer_info.hostname, &peer_info.index);
			else if (func == OMX_CMD_PEER_FROM_HOSTNAME)
				ret =
				    omx_peer_lookup_by_hostname
				    (peer_info.hostname,
				     &peer_info.board_addr, &peer_info.index);

			/* It's OK if we return an error, just send the response */
			if (ret < 0) {
				dprintk_deb
				    ("Failed to execute cmd=%lx\n",
				     (unsigned long)func);
			} else {
				memcpy(&resp->data.mpi.info, &peer_info,
				       sizeof(struct omx_cmd_misc_peer_info));
				memcpy(resp->data.mpi.info.hostname,
				       peer_info.hostname,
				       sizeof(peer_info.hostname));
				dprintk_deb
				    ("peer_info.index = %#lx, ret = %d\n",
				     (unsigned long)resp->data.mpi.info.index,
				     resp->data.mpi.ret);
				dprintk_deb("peer_info.board_addr = %#llx\n",
					    resp->data.mpi.info.board_addr);
				dprintk_deb("peer_info.hostname =  %s\n",
					    resp->data.mpi.info.hostname);
			}

			resp->func = req->func;
			resp->ret = ret;

			break;
		}

	case OMX_CMD_GET_ENDPOINT_INFO:{
			struct omx_endpoint *endpoint;
			struct omx_endpoint_info get_endpoint_info;
			dprintk_deb
			    ("received frontend request: OMX_CMD_GET_ENDPOINT_INFO, param=%lx\n",
			     sizeof(struct omx_cmd_xen_get_board_info));

			endpoint = omx_xenback_get_endpoint(be, req);

			//memset(&resp->data.gei, 0, sizeof(resp->data.gei));
			dprintk_deb("Got endpoint %d @ %#lx\n", eid,
				    (unsigned long)endpoint);
			BUG_ON(!endpoint);
			omx_endpoint_get_info(endpoint->board_index,
					      endpoint->endpoint_index,
					      &get_endpoint_info);
			memcpy(&resp->data.gei.info, &get_endpoint_info,
			       sizeof(struct omx_endpoint_info));

			omx_xenback_prepare_response(endpoint, req, resp, 0);

			break;
		}
	case OMX_CMD_XEN_GET_BOARD_COUNT:{
			uint32_t count;
			dprintk_deb
			    ("received frontend request: OMX_CMD_GET_BOARD_COUNT, param=%lx\n",
			     sizeof(struct omx_cmd_xen_get_board_count));

			count = omx_ifaces_get_count();

			resp->func = OMX_CMD_XEN_GET_BOARD_COUNT;
			resp->data.gbc.board_count = count;

			break;
		}
	case OMX_CMD_XEN_PEER_TABLE_GET_STATE:{
			uint32_t bi;
			struct omx_cmd_peer_table_state state;
			dprintk_deb
			    ("received frontend request: OMX_CMD_PEER_TABLE_GET_STATE, param=%lx\n",
			     sizeof(struct omx_cmd_xen_peer_table_state));

			bi = req->board_index;

			dprintk_deb("got (%d)\n", bi);

			omx_peer_table_get_state(&state);

			memcpy(&resp->data.pts.state, &state, sizeof(state));

			dprintk_deb("status= %#lx, version=%#lx, size=%lx"
				    " mapper_id = %llx\n",
				    state.status, state.version,
				    state.size, state.mapper_id);
			resp->func = OMX_CMD_XEN_PEER_TABLE_GET_STATE;
			resp->board_index = bi;
			resp->ret = ret;

			break;
		}
	case OMX_CMD_XEN_PEER_TABLE_SET_STATE:{
			uint32_t bi;
			struct omx_cmd_peer_table_state state;
			dprintk_deb
			    ("received frontend request: OMX_CMD_PEER_TABLE_SET_STATE, param=%lx\n",
			     sizeof(struct omx_cmd_xen_peer_table_state));

			bi = req->board_index;

			dprintk_deb("got (%d)\n", bi);

			memcpy(&state, &req->data.pts.state, sizeof(state));

			/* FIXME: Now that we've got the frontend's peer table state,
			 * figure out what to do next ;-) Leaving it blank atm */

			resp->func = OMX_CMD_XEN_PEER_TABLE_SET_STATE;
			resp->board_index = bi;
			resp->ret = ret;

			break;
		}
	case OMX_CMD_XEN_SET_HOSTNAME:{
			uint32_t bi;
			struct omx_cmd_set_hostname set_hostname;
			dprintk_deb
			    ("received frontend request: OMX_CMD_XEN_SET_HOSTNAME, param=%lx\n",
			     sizeof(struct omx_cmd_xen_set_hostname));

			bi = req->board_index;

			dprintk_deb("got (%d)\n", bi);

			/* FIXME: Now that we've got the frontend's hostname,
			 * figure out what to do next ;-) Leaving it blank atm */

			//memcpy(set_hostname.hostname, req->data.sh.hostname, OMX_HOSTNAMELEN_MAX);
			ret = omx_iface_set_hostname(bi, req->data.sh.hostname);
			if (ret) {
				printk_err
				    ("Cannot set hostname %s for board id=%#lx\n",
				     req->data.sh.hostname, bi);
			}
			resp->func = OMX_CMD_XEN_SET_HOSTNAME;
			resp->board_index = bi;
			resp->ret = ret;

			break;
		}
	case OMX_CMD_GET_BOARD_INFO:{
			struct omx_endpoint *endpoint;
			struct omx_board_info get_board_info;
			dprintk_deb
			    ("received frontend request: OMX_CMD_GET_BOARD_INFO, param=%lx\n",
			     sizeof(struct omx_cmd_xen_get_board_info));

			endpoint = omx_xenback_get_endpoint(be, req);
			BUG_ON(!endpoint);
			ret =
			    omx_iface_get_info(endpoint->board_index,
					       &get_board_info);

			if (ret < 0) {
				printk_err
				    ("Failed to execute cmd=%lx\n",
				     (unsigned long)func);
			} else {
				//memset(&resp->data.gbi, 0, sizeof(resp->data.gbi));
				memcpy(&resp->data.gbi.info, &get_board_info,
				       sizeof(struct omx_board_info));
			}

			omx_xenback_prepare_response(endpoint, req, resp, ret);

			break;
		}
	case OMX_CMD_XEN_OPEN_ENDPOINT:{
			dprintk_deb
			    ("received frontend request: OMX_CMD_XEN_OPEN_ENDPOINT, param=%lx\n",
			     sizeof(struct omx_ring_msg_endpoint));
			ret = omx_xen_endpoint_open(be, req);
			if (ret < 0) {
				printk_err
				    ("Endpoint could not be opened ret = %d!\n",
				     ret);
			}
			//memset(&resp->data.endpoint, 0, sizeof(resp->data.endpoint));
			resp->func = OMX_CMD_XEN_OPEN_ENDPOINT;
			resp->data.endpoint.endpoint =
			    req->data.endpoint.endpoint;
			resp->eid = req->eid;
			resp->board_index = req->board_index;
			resp->ret = ret;

			omx_xen_timers_reset();
			break;
		}
	case OMX_CMD_XEN_CLOSE_ENDPOINT:{
			dprintk_deb
			    ("received frontend request: OMX_CMD_XEN_CLOSE_ENDPOINT, param=%lx\n",
			     sizeof(struct omx_ring_msg_endpoint));
			printk_timers();
			ret = omx_xen_endpoint_close(be, req);
			if (ret < 0) {
				printk_err
				    ("Endpoint could not be Closed ret = %d!\n",
				     ret);
				//goto out;
			}
			//memset(&resp->data.endpoint, 0, sizeof(resp->data.endpoint));
			resp->func = OMX_CMD_XEN_CLOSE_ENDPOINT;
			resp->eid = req->eid;
			resp->board_index = req->board_index;
			resp->ret = ret;

			break;
		}
	case OMX_CMD_XEN_CREATE_USER_REGION:{
			uint8_t eid;
			uint32_t nr_segments, id, nr_grefs, nr_pages, sid;
			uint64_t vaddr;
			int i;
			struct omx_endpoint *endpoint;
			dprintk_deb
			    ("received frontend request: OMX_CMD_XEN_CREATE_USER_REGION, param=%lx\n",
			     sizeof(struct omx_ring_msg_create_user_region));
			id = req->data.cur.id;
			eid = req->data.cur.eid;
			vaddr = req->data.cur.vaddr;
			nr_grefs = req->data.cur.nr_grefs;
			nr_pages = req->data.cur.nr_pages;
			nr_segments = req->data.cur.nr_segments;
			endpoint = omx_xenif->be->omxdev->endpoints[eid];

			dprintk_deb("reg id=%u, nr_segments=%u, eid=%u"
				    " vaddr=%#lx, nr_pages=%lu, nr_grefs=%u",
				    id, nr_segments, eid,
				    (unsigned long)vaddr, (unsigned long)
				    nr_pages, nr_grefs);

			ret =
			    omx_xen_create_user_region(omx_xenif, id,
						       vaddr,
						       nr_segments,
						       nr_pages, nr_grefs, eid);

			for (i = 0; i < nr_segments; i++) {
				struct omx_ring_msg_register_user_segment *seg;
				seg = &req->data.cur.segs[i];

				sid = seg->sid;
				id = seg->rid;
				eid = seg->eid;

				ret =
				    omx_xen_register_user_segment(omx_xenif,
								  seg);

				if (ret)
					printk_err
					    ("Failed to register user segment %u\n",
					     sid);
			}
			resp->func = OMX_CMD_XEN_CREATE_USER_REGION;
			resp->data.cur.id = id;
			resp->data.cur.eid = eid;

			if (ret < 0) {
				printk_err("Failed to reg\n");
				resp->data.cur.status = 0x1;
			}
			else
				resp->data.cur.status = 0x0;
			rmb();
			/* FIXME: Really buggy/experimental stuff!!
			 * We actually dereference the frontend's endpoint
			 * structure to notify the frontend that the region
			 * is ready and let the IOCTL return, to proceed with
			 * the rest of the operations (sending/receiving etc.)
			 * If we don't do it like this, the only thing that's
			 * left to be done for the region to be marked ready is
			 * to push a response from here and let it reach the frontend.
			 * why wait for so long ?*/
#if 0
			if (endpoint->fe_endpoint->special_status == 3)
				endpoint->fe_endpoint->special_status = 4;
			else
				printk_err("status is invalid, %u\n",
					   endpoint->
					   fe_endpoint->special_status);
#endif

			wmb();
			break;
		}
	case OMX_CMD_XEN_DESTROY_USER_REGION:{
			uint32_t id, seqnum;
			uint8_t eid;
			uint32_t sid;
			int i;

			dprintk_deb
			    ("received frontend request: OMX_CMD_XEN_DESTROY_USER_REGION, param=%lx\n",
			     sizeof(struct omx_ring_msg_destroy_user_region));
			id = req->data.dur.id;
			seqnum = req->data.dur.seqnum;
			eid = req->data.dur.eid;

			for (i = 0; i < req->data.dur.nr_segments; i++) {
				struct omx_ring_msg_deregister_user_segment
				*seg;
				seg = &req->data.dur.segs[i];

				sid = seg->sid;
				id = seg->rid;
				eid = seg->eid;
				dprintk_deb("reg id=%u, sid=%u, eid=%u\n",
					    id, sid, eid);
				ret =
				    omx_xen_deregister_user_segment(omx_xenif,
								    id, sid,
								    eid);

				if (ret)
					dprintk_deb
					    ("Failed to deregister user segment %u\n",
					     sid);
			}

			dprintk_deb
			    ("de-reg id=%lx, seqnum=%lx, eid=%u\n",
			     (unsigned long)id, (unsigned long)seqnum, eid);

			ret =
			    omx_xen_destroy_user_region(omx_xenif, id,
							seqnum, eid);
			//memset(&resp->data.dur, 0, sizeof(resp->data.dur));
			resp->func = OMX_CMD_XEN_DESTROY_USER_REGION;
			resp->data.dur.id = id;
			resp->data.dur.eid = eid;
			resp->data.dur.region = req->data.dur.region;
			if (ret < 0) {
				printk_err("Failed to dereg\n");
				resp->data.dur.status = 0x1;
			}
			else
				resp->data.dur.status = 0x0;

			break;
		}
	default:{
			printk_err("No usefull command received: %x\n", func);
			break;

		}
	}
out:
	dprintk_out();
	return ret;
}

int omx_xenback_process_specific(omx_xenif_t * omx_xenif, uint32_t func, struct
				 omx_xenif_request *req,
				 struct omx_xenif_response *resp)
{
	struct backend_info *be = omx_xenif->be;
	struct omx_endpoint *endpoint;
	unsigned long flags;
	int ret = 0;

	dprintk_in();
	spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
	endpoint = omx_xenback_get_endpoint(be, req);
	spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);

	switch (func) {
	case OMX_CMD_PULL:{
			dprintk_deb
			    ("received frontend request: OMX_CMD_PULL, param=%lx\n",
			     sizeof(struct omx_cmd_xen_pull));
			ret = omx_ioctl_pull(endpoint, &req->data.pull.pull);

			break;
		}
	case OMX_CMD_SEND_RNDV:{
			struct omx_cmd_send_rndv send_rndv;
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_RNDV, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_rndv));
			spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);

			/* getting connect request structure */
			memcpy(&send_rndv, &req->data.send_rndv.rndv,
			       sizeof(send_rndv));

			//ret = omx_ioctl_send_rndv(endpoint, &req->data.send_rndv.rndv);
			spin_unlock_irqrestore
			    (&omx_xenif->omx_ring_lock, flags);
			ret = omx_ioctl_send_rndv(endpoint, &send_rndv);

			//memset(&resp->data.send_rndv, 0, sizeof(resp->data.send_rndv));
			break;
		}
	case OMX_CMD_SEND_MEDIUMSQ_FRAG:{
			struct omx_cmd_xen_send_mediumsq_frag
			    xen_send_mediumsq_frag;
			struct omx_cmd_send_mediumsq_frag send_mediumsq_frag;
			//uint16_t checksum = 0;
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_MEDIUMSQ_FRAG, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_mediumva));
			memcpy(&xen_send_mediumsq_frag,
			       &req->data.send_mediumsq_frag,
			       sizeof(xen_send_mediumsq_frag));
			memcpy(&xen_send_mediumsq_frag.mediumsq_frag,
			       &req->data.send_mediumsq_frag.mediumsq_frag,
			       sizeof(send_mediumsq_frag));
			ret =
			    omx_xen_setup_and_send_mediumsq_frag
			    (endpoint, &xen_send_mediumsq_frag);

			if (ret) {
				printk_err("Medium SQ_FRAG error\n");
			}
			//memset(&resp->data.send_small, 0, sizeof(resp->data.send_small));

			break;
		}
	case OMX_CMD_SEND_MEDIUMVA:{
			struct omx_cmd_xen_send_mediumva xen_send_mediumva;
			struct omx_cmd_send_mediumva send_mediumva;
			//uint16_t checksum = 0;
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_MEDIUMVA, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_mediumva));
#if !MEDIUMVA_FAKE
			memcpy(&xen_send_mediumva,
			       &req->data.send_mediumva,
			       sizeof(xen_send_mediumva));
			memcpy(&xen_send_mediumva.mediumva,
			       &req->data.send_mediumva.mediumva,
			       sizeof(send_mediumva));
			ret =
			    omx_xen_setup_and_send_mediumva(endpoint,
							    &xen_send_mediumva);
#else
			checksum = req->data.send_mediumva.mediumva.checksum;
			req->data.send_small.small.length = 128;
			req->data.send_small.small.checksum = checksum;
			req->data.send_small.small.vaddr =
			    (uint64_t) req->data.send_small.data;
			ret =
			    omx_ioctl_send_small(endpoint,
						 &req->data.send_small.small);
#endif
			if (ret) {
				printk_err("Medium VA error\n");
			}
			//memset(&resp->data.send_small, 0, sizeof(resp->data.send_small));

			break;
		}
	case OMX_CMD_SEND_SMALL:{
			int ret = 0;
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_SMALL, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_small));
			spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
			req->data.send_small.small.vaddr =
			    (uint64_t) req->data.send_small.data;
			spin_unlock_irqrestore
			    (&omx_xenif->omx_ring_lock, flags);
			//dump_xen_send_small(&req->data.send_small);
			ret =
			    omx_ioctl_send_small(endpoint,
						 &req->data.send_small.small);
			//memset(&resp->data.send_small, 0, sizeof(resp->data.send_small));
			break;
		}
	case OMX_CMD_SEND_TINY:{
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_TINY, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_tiny));
			ret = omx_ioctl_send_tiny(endpoint, &req->data.send_tiny.tiny);	//&tiny.tiny);
			//memset(&resp->data.send_tiny, 0, sizeof(resp->data.send_tiny));
			break;
		}
	case OMX_CMD_SEND_NOTIFY:{
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_NOTIFY, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_notify));

			//dprintk(SEND, "Sending Notifies\n");
			ret =
			    omx_ioctl_send_notify(endpoint,
						  &req->data.send_notify.
						  notify);

			//memset(&resp->data.send_notify, 0, sizeof(resp->data.send_notify));

			break;
		}
	case OMX_CMD_SEND_LIBACK:{
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_LIBACK, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_liback));

			//dump_xen_send_liback(&req->data.send_liback);
			ret =
			    omx_ioctl_send_liback(endpoint,
						  &req->data.send_liback.
						  liback);

			//memset(&resp->data.send_liback, 0, sizeof(resp->data.send_liback));

			break;
		}
	case OMX_CMD_SEND_CONNECT_REQUEST:{
			struct omx_cmd_send_connect_request connect;
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_CONNECT_REQUEST, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_connect_request));
			spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
			/* getting connect request structure */
			//dump_xen_send_connect_request(&req-> data.send_connect_request);
			memcpy(&connect,
			       &req->data.send_connect_request.request,
			       sizeof(connect));
			spin_unlock_irqrestore(&omx_xenif->omx_ring_lock,
					       flags);
			ret =
			    omx_ioctl_send_connect_request(endpoint, &connect);
			//memset(&resp->data.send_connect_request, 0, sizeof(resp->data.send_connect_request));
			break;
		}
	case OMX_CMD_SEND_CONNECT_REPLY:{
			struct omx_cmd_send_connect_reply reply;
			dprintk_deb
			    ("received frontend request: OMX_CMD_SEND_CONNECT_REPLY, param=%lx\n",
			     sizeof(struct omx_cmd_xen_send_connect_reply));
			spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
			/* getting connect request structure */
			//dump_xen_send_connect_reply(&req->data.send_connect_reply);
			memcpy(&reply,
			       &req->data.send_connect_reply.reply,
			       sizeof(reply));
			spin_unlock_irqrestore
			    (&omx_xenif->omx_ring_lock, flags);
			ret = omx_ioctl_send_connect_reply(endpoint, &reply);
			break;
		}
	default:{
			printk_err("No usefull command received: %x\n", func);
			break;

		}
	}

	spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
	omx_xenback_prepare_response(endpoint, req, resp, ret);
	spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);

out:
	dprintk_out();
	return ret;

}
int omx_xen_process_message(omx_xenif_t * omx_xenif,
			    struct omx_xenif_back_ring *ring)
{
	RING_IDX cons;
	RING_IDX prod;
	struct omx_xenif_request *req;
	struct backend_info *be;
	struct omx_xenif_response *resp;
	unsigned long flags;
	int ret = 0;
	uint32_t func;

	dprintk_in();

	if (unlikely(!omx_xenif)) {
		printk_err("Got NULL for omx_xenif, aborting!\n");
		goto out;
	}
	be = omx_xenif->be;
	if (unlikely(!be)) {
		printk_err("Got NULL for be, aborting!\n");
		goto out;
	}
	if (!ring) {
		printk_err("No ring to process\n");
		ret = -EINVAL;
		goto out;
	}

	cons = ring->req_cons;
	prod = ring->sring->req_prod;

	dprintk_deb
	    ("ring=%p, consumed = %d, rsp_prod_pvt=%d requests_produced= %d\n",
	     ring, cons, ring->rsp_prod_pvt, prod);
	rmb();
	while (cons != prod) {
		dprintk_deb("omx_xenif->ring.req_cons=%d, i=%d, rp=%d\n",
			    omx_xenif->ring.req_cons, omx_xenif->ring.req_cons,
			    omx_xenif->ring.sring->req_prod);

		spin_lock_irqsave(&omx_xenif->omx_ring_lock, flags);
		if (RING_REQUEST_CONS_OVERFLOW(ring, cons)) {
			printk_err("Overflow!\n");
			dprintk_inf
			    ("ring=%p, consumed = %d, rsp_prod_pvt=%d requests_produced= %d\n",
			     ring, cons, ring->rsp_prod_pvt, prod);
			goto out_with_lock;
		}
		req = RING_GET_REQUEST(ring, cons++);
		if (unlikely(!req)) {
			printk_err("Got NULL for req, aborting!\n");
			goto out_with_lock;
		}
		func = req->func;
		if (unlikely(!func)) {
			printk_err("%s: Got NULL for req->func, aborting!\n",
				   __func__);
			goto out_with_lock;
		}
		dprintk_deb(KERN_INFO "func = %#x, requests_produced= %d\n",
			    func, omx_xenif->ring.sring->req_prod);

		resp =
		    RING_GET_RESPONSE(&(omx_xenif->ring),
				      omx_xenif->ring.rsp_prod_pvt++);
		if (unlikely(!resp)) {
			printk_err("Got NULL for resp, aborting!\n");
			goto out_with_lock;
		}

		spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);
		switch (func) {
			case OMX_CMD_PEER_FROM_INDEX:
			case OMX_CMD_PEER_FROM_ADDR:
			case OMX_CMD_PEER_FROM_HOSTNAME:
			case OMX_CMD_GET_ENDPOINT_INFO:
			case OMX_CMD_XEN_GET_BOARD_COUNT:
			case OMX_CMD_XEN_PEER_TABLE_GET_STATE:
			case OMX_CMD_XEN_PEER_TABLE_SET_STATE:
			case OMX_CMD_XEN_SET_HOSTNAME:
			case OMX_CMD_GET_BOARD_INFO:
			case OMX_CMD_XEN_OPEN_ENDPOINT:
			case OMX_CMD_XEN_CLOSE_ENDPOINT:
			case OMX_CMD_XEN_CREATE_USER_REGION:
			case OMX_CMD_XEN_DESTROY_USER_REGION:
				ret = omx_xenback_process_misc(omx_xenif, func, req, resp);
				break;
			default:
				ret = omx_xenback_process_specific(omx_xenif, func, req, resp);
		}
		if (ret) {
			printk_err("Failed, ret = %d\n", ret);
		}

		dprintk_deb("response ready (%#llx), id=%#x sending to %u\n",
			    (unsigned long long)resp, resp->func,
			    omx_xenif->be->evtchn.port);

	}
	ring->req_cons = cons;
	wmb();
	goto out;

out_with_lock:
	spin_unlock_irqrestore(&omx_xenif->omx_ring_lock, flags);
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
