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

#include "omx_common.h"
#include "omx_reg.h"
#include "omx_endpoint.h"

//#define TIMERS_ENABLED
#include "omx_xen_timers.h"
//#define OMX_XEN_NOWAIT
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xen_lib.h"
#include "omx_xenfront.h"
#include "omx_xenfront_send.h"

extern timers_t t1,t2,t3,t4;
/* In this set of functions, we copy user data directly to the ring structure.
 * FIXME: There's a lot of testing to be done, to make sure that there are no
 * corruption or concurrency issues
 */
int omx_ioctl_xen_send_tiny(struct omx_endpoint *endpoint, void __user * uparam)
{
	struct omx_cmd_xen_send_tiny *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	uint32_t length = 0;
	int ret = 0;

	dprintk_in();

	//TIMER_START(&t1);
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_TINY;
	cmd = &ring_req->data.send_tiny;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret =
	    copy_from_user(&cmd->tiny.hdr,
			   &((struct omx_cmd_send_tiny __user *)uparam)->hdr,
			   sizeof(struct omx_cmd_send_tiny_hdr));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd->tiny.hdr.length;
	if (unlikely(length > OMX_TINY_MSG_LENGTH_MAX)) {
		printk(KERN_ERR
		       "Open-MX: Cannot send more than %d as a tiny (tried %d)\n",
		       OMX_TINY_MSG_LENGTH_MAX, length);
		ret = -EINVAL;
		goto out;
	}

	ret =
	    copy_from_user(cmd->tiny.data,
			   &((struct omx_cmd_send_tiny __user *)uparam)->data,
			   length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send tiny cmd data\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd->tiny.hdr.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->tiny.hdr.shared = 0;
	}
	//dump_xen_send_tiny(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
#ifndef OMX_XEN_NOWAIT
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EINVAL;
		printk_err("Backend failed to ACK send tiny \n");
	}
#endif
out:
	//TIMER_STOP(&t1);
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_mediumva(struct omx_endpoint *endpoint,
				void __user * uparam)
{
	struct omx_cmd_xen_send_mediumva *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	struct omx_cmd_user_segment *usegs, *cur_useg;
	uint32_t msg_length, remaining, cur_useg_remaining;
	void __user *cur_udata;
	uint32_t nseg;
	int frags_nr;
	int i;
	grant_ref_t *grefs;
	struct page **pages;
	uint16_t offset;
	uint8_t nr_pages;
	uint32_t aligned_vaddr;
	grant_ref_t gref_head;
	int ret = 0;

	dprintk_in();

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_MEDIUMVA;
	cmd = &ring_req->data.send_mediumva;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;
	//data = cmd->data;

	ret =
	    copy_from_user(&cmd->mediumva, uparam,
			   sizeof(struct omx_cmd_send_mediumva));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send mediumva cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	msg_length = cmd->mediumva.length;
#ifdef OMX_MX_WIRE_COMPAT
	if (unlikely(msg_length > OMX__MX_MEDIUM_MSG_LENGTH_MAX)) {
		printk(KERN_ERR
		       "Open-MX: Cannot send more than %ld as a mediumva in MX-wire-compat mode (tried %ld)\n",
		       (unsigned long)OMX__MX_MEDIUM_MSG_LENGTH_MAX,
		       (unsigned long)msg_length);
		ret = -EINVAL;
		goto out;
	}
#endif
	frags_nr =
	    (msg_length + OMX_MEDIUM_FRAG_LENGTH_MAX -
	     1) / OMX_MEDIUM_FRAG_LENGTH_MAX;
	nr_pages = (msg_length + PAGE_SIZE - 1) / PAGE_SIZE;
	dprintk_deb("frags_nr = %#x, msg_length = %d, nr_pages =%#x\n",
		    frags_nr, msg_length, nr_pages);
	nseg = cmd->mediumva.nr_segments;
	if (nseg > 1) {
		printk_err("Does not support > 1 segments yet, sorry:S\n");
		ret = -EINVAL;
		goto out;
	}

	if (cmd->mediumva.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->mediumva.shared = 0;
	}

	/* get user segments */
	usegs = kmalloc(nseg * sizeof(struct omx_cmd_user_segment), GFP_KERNEL);
	if (!usegs) {
		printk(KERN_ERR
		       "Open-MX: Cannot allocate segments for mediumva\n");
		ret = -ENOMEM;
		goto out;
	}
	ret =
	    copy_from_user(usegs,
			   (void __user *)(unsigned long)cmd->mediumva.segments,
			   nseg * sizeof(struct omx_cmd_user_segment));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read mediumva segments cmd\n");
		ret = -EFAULT;
		goto out;
	}

	/* compute the segments length */
	remaining = 0;
	remaining += usegs[0].len;
	if (remaining != msg_length) {
		printk(KERN_ERR
		       "Open-MX: Cannot send mediumva without enough data in segments (%ld instead of %ld)\n",
		       (unsigned long)remaining, (unsigned long)msg_length);
		ret = -EINVAL;
		goto out;
	}

	/* initialize position in segments */
	cur_useg = &usegs[0];
	cur_useg_remaining = cur_useg->len;
	cur_udata = (__user void *)(unsigned long)cur_useg->vaddr;
	offset = (unsigned long)cur_useg->vaddr & ~PAGE_MASK;
	aligned_vaddr = cur_useg->vaddr & PAGE_MASK;

	if ((offset + cur_useg->len) > PAGE_SIZE) {
		nr_pages++;
	}
	pages = kmalloc(sizeof(struct page *) * nr_pages, GFP_KERNEL);
	if (!pages) {
		printk_err("Failed to kmalloc pages\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = get_user_pages_fast(aligned_vaddr, nr_pages, 1, pages);
	if (ret != nr_pages) {
		printk_err
		    ("get_user_pages_fast FAILED!, ret = %d, nr_pages =%d\n",
		     ret, nr_pages);
		ret = -ENOMEM;
		goto out;
	}

	ret = gnttab_alloc_grant_references(nr_pages, &gref_head);
	if (ret < 0) {
		printk_err("Cannot allocate grant references\n");
		goto out;
	}

	grefs = kmalloc(sizeof(grant_ref_t) * nr_pages, GFP_KERNEL);
	if (!pages) {
		printk_err("Failed to kmalloc grefs\n");
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < nr_pages; i++) {
		struct page *single_page;
		unsigned long mfn;
		grant_ref_t gref;

		single_page = pages[i];
		mfn = pfn_to_mfn(page_to_pfn(single_page));
		gref = gnttab_claim_grant_reference(&gref_head);
		if (!gref) {
			printk_err("cannot claim grant reference\n");
			ret = -EINVAL;
			goto out;
		}
		gnttab_grant_foreign_access_ref(gref, 0, mfn, 0);
		grefs[i] = gref;
		//printk(KERN_INFO "grefs[%d] = %x, mfn=%#lx\n", i, grefs[i], mfn);

		/* FIXME */
#if 0
		cur_udata += PAGE_SIZE;
		cur_useg_remaining -= i;
#endif

#if 0
		if (!(cur_useg_remaining > 0)) {
			cur_useg++;
			cur_useg_remaining = cur_useg->len;
			cur_udata =
			    (__user void *)(unsigned long)cur_useg->vaddr;
			offset = (unsigned long)cur_useg->vaddr & ~PAGE_MASK;
			dprintk_deb("offset = %#lx\n", offset);
		}
#endif
	}
	dprintk_deb("frags_nr = %#x, i = %d, nr_pages= %#x\n", frags_nr, i,
		    nr_pages);

	memcpy(cmd->grefs, grefs, nr_pages * sizeof(grant_ref_t));
	cmd->nr_pages = nr_pages;
	cmd->first_page_offset = offset;
	//printk(KERN_INFO "first-page-offset = %#x\n", offset);
	//grefs[i] = -1;
	//pages[i] = -1;

	//dump_xen_send_mediumva(cmd);
#if 0
	/* copy the data right after the header */
	ret =
	    copy_from_user(data, (__user void *)(unsigned long)cmd->small.vaddr,
			   length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send small cmd data\n");
		ret = -EFAULT;
		goto out;
	}
#endif
	omx_poke_dom0(endpoint->fe, ring_req);
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EFAULT;
		printk_err("Backend failed to ACK send mediumva\n");
	}

	for (i = 0; i < nr_pages; i++) {
		struct page *single_page;
		single_page = pages[i];
		dprintk_deb("grefs[%d] = %u\n", i, grefs[i]);
		ret = gnttab_end_foreign_access_ref(grefs[i], 0);
		if (!ret) {
			printk_err("Cannot end foreign access\n");
			ret = -EINVAL;
			goto out;
		}
		gnttab_release_grant_reference(&gref_head, grefs[i]);
		put_page(single_page);

		/* FIXME */
#if 0
		cur_udata += PAGE_SIZE;
		cur_useg_remaining -= i;
#endif

#if 0
		if (!(cur_useg_remaining > 0)) {
			cur_useg++;
			cur_useg_remaining = cur_useg->len;
			cur_udata =
			    (__user void *)(unsigned long)cur_useg->vaddr;
			offset = (unsigned long)cur_useg->vaddr & ~PAGE_MASK;
			dprintk_deb("offset = %#lx\n", offset);
		}
#endif
	}
	gnttab_free_grant_references(gref_head);
	kfree(pages);
	kfree(usegs);

out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_mediumsq_frag(struct omx_endpoint *endpoint,
			     void __user * uparam)
{
	struct omx_cmd_xen_send_mediumsq_frag *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;
        uint32_t sendq_offset;
        uint32_t frag_length;


	dprintk_in();

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_MEDIUMSQ_FRAG;
	cmd = &ring_req->data.send_mediumsq_frag;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret =
	    copy_from_user(&cmd->mediumsq_frag, uparam,
			   sizeof(struct omx_cmd_send_mediumsq_frag));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send mediumsq_frag cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

        frag_length = cmd->mediumsq_frag.frag_length;
        if (unlikely(frag_length > OMX_SENDQ_ENTRY_SIZE)) {
                printk(KERN_ERR "Open-MX: Cannot send more than %ld as a mediumsq frag (tried %ld)\n",
                       OMX_SENDQ_ENTRY_SIZE, (unsigned long) frag_length);
                ret = -EINVAL;
                goto out;
        }

        sendq_offset = cmd->mediumsq_frag.sendq_offset;
        if (unlikely(sendq_offset >= OMX_SENDQ_SIZE)) {
                printk(KERN_ERR "Open-MX: Cannot send mediumsq fragment from sendq offset %ld (max %ld)\n",
                       (unsigned long) sendq_offset, (unsigned long) OMX_SENDQ_SIZE);
                ret = -EINVAL;
                goto out;
        }

	if (cmd->mediumsq_frag.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->mediumsq_frag.shared = 0;
	}


	omx_poke_dom0(endpoint->fe, ring_req);
#ifndef OMX_XEN_NOWAIT
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EFAULT;
		printk_err("Backend failed to ACK send mediumsq frag\n");
	}
#endif
out:
	dprintk_out();
	return ret;
}
int omx_ioctl_xen_send_small(struct omx_endpoint *endpoint,
			     void __user * uparam)
{
	struct omx_cmd_xen_send_small *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	uint32_t length = 0;
	int ret = 0;
	char *data;

	dprintk_in();

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_SMALL;
	cmd = &ring_req->data.send_small;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;
	data = cmd->data;

	ret =
	    copy_from_user(&cmd->small, uparam,
			   sizeof(struct omx_cmd_send_small));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send small cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	length = cmd->small.length;
	if (unlikely(length > OMX_SMALL_MSG_LENGTH_MAX)) {
		printk(KERN_ERR
		       "Open-MX: Cannot send more than %d as a small (tried %d)\n",
		       OMX_SMALL_MSG_LENGTH_MAX, length);
		ret = -EINVAL;
		goto out;
	}

	if (cmd->small.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->small.shared = 0;
	}
	//dump_xen_send_small(cmd);
	/* copy the data right after the header */
	ret =
	    copy_from_user(data, (__user void *)(unsigned long)cmd->small.vaddr,
			   length);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send small cmd data\n");
		ret = -EFAULT;
		goto out;
	}

	omx_poke_dom0(endpoint->fe, ring_req);
#ifndef OMX_XEN_NOWAIT
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EFAULT;
		printk_err("Backend failed to ACK send small\n");
	}
#endif
out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_notify(struct omx_endpoint *endpoint,
			      void __user * uparam)
{
	struct omx_cmd_xen_send_notify *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_NOTIFY;
	cmd = &ring_req->data.send_notify;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret =
	    copy_from_user(&cmd->notify, uparam,
			   sizeof(struct omx_cmd_send_notify));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send connect request cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd->notify.shared) {
		cmd->notify.shared = 0;
		/* FIXME: handle the intra-node/VM case */
	}

	dump_xen_send_notify(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else
		ret = -EFAULT;
out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_connect_request(struct omx_endpoint *endpoint,
				       void __user * uparam)
{
	struct omx_cmd_xen_send_connect_request *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();

	/* fill omx header */
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_CONNECT_REQUEST;
	cmd = &ring_req->data.send_connect_request;

	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;
	ret =
	    copy_from_user(&cmd->request, uparam,
			   sizeof(struct omx_cmd_send_connect_request));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send connect request cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (!cmd->request.shared_disabled) {
		/* FIXME: handle the intra-node/VM case */
		cmd->request.shared_disabled = 1;
	}

	dump_xen_send_connect_request(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EFAULT;
		printk_err("Backend failed to ACK send connect\n");
	}

out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_connect_reply(struct omx_endpoint *endpoint,
				     void __user * uparam)
{
	struct omx_cmd_xen_send_connect_reply *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_CONNECT_REPLY;
	cmd = &ring_req->data.send_connect_reply;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret =
	    copy_from_user(&cmd->reply, uparam,
			   sizeof(struct omx_cmd_send_connect_reply));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send connect request reply cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (!cmd->reply.shared_disabled) {
		/* FIXME: handle the intra-node/VM case */
		cmd->reply.shared_disabled = 1;
	}

	dump_xen_send_connect_reply(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EFAULT;
		printk_err("Backend failed to ACK send connect reply\n");
	}
out:

	dprintk_out();
	return ret;
}

int omx_ioctl_xen_pull(struct omx_endpoint *endpoint, void __user * uparam)
{
	struct omx_cmd_xen_pull *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_PULL;
	cmd = &ring_req->data.pull;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret = copy_from_user(&cmd->pull, uparam, sizeof(struct omx_cmd_pull));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send pull cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd->pull.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->pull.shared = 0;
	}

	dump_xen_pull(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
#ifndef OMX_XEN_NOWAIT
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else
		ret = -EFAULT;
#endif

out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_rndv(struct omx_endpoint *endpoint, void __user * uparam)
{
	struct omx_cmd_xen_send_rndv *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_RNDV;
	cmd = &ring_req->data.send_rndv;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret =
	    copy_from_user(&cmd->rndv, uparam,
			   sizeof(struct omx_cmd_send_rndv));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read send rndv cmd\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd->rndv.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->rndv.shared = 0;
	}

	/* fill omx header */
	dump_xen_send_rndv(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
#ifndef OMX_XEN_NOWAIT
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EINVAL;
		printk_err("Backend failed to ACK send rndv \n");
	}
#endif
#if 0
	printk(KERN_INFO
	       "%s: delaying on purpose to understand what is going on!\n",
	       __func__);
	udelay(10000);
#endif

out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_send_liback(struct omx_endpoint *endpoint,
			      void __user * uparam)
{
	struct omx_cmd_xen_send_liback *cmd;
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_SEND_LIBACK;
	cmd = &ring_req->data.send_liback;
	cmd->board_index = endpoint->board_index;
	cmd->eid = endpoint->endpoint_index;

	ret =
	    copy_from_user(&cmd->liback, uparam,
			   sizeof(struct omx_cmd_send_liback));
	if (unlikely(ret != 0)) {
		printk(KERN_ERR
		       "Open-MX: Failed to read send connect request cmd hdr\n");
		ret = -EFAULT;
		goto out;
	}

	if (cmd->liback.shared) {
		/* FIXME: handle the intra-node/VM case */
		cmd->liback.shared = 0;
	}

	/* fill omx header */

	dump_xen_send_liback(cmd);
	omx_poke_dom0(endpoint->fe, ring_req);
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_DONE)
		ret = 0;
	else {
		ret = -EFAULT;
		printk_err("Backend failed to ACK send liback \n");
	}
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
