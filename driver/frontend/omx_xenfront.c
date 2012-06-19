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
#define OMX_XEN_POLL_HARD_LIMIT 50000000UL
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xen_lib.h"
#include "omx_xenfront.h"
#include "omx_xenfront_endpoint.h"
#include "omx_xenfront_reg.h"

timers_t t1,t2,t3,t4,t5,t6,t7;
void dump_ring_req(struct omx_xenif_request *req)
{
	char data[16];
	void *longreq;
	int i, j;
	longreq = (unsigned long *)req;
	for (i = 0; i < sizeof(*req); i += 8) {
		if (!longreq)
			break;
		for (j = 0; j < 8; j++) {
			data[j] = *(char *)longreq;
			longreq++;
		}
		printk_err("%.2u %.2x %.2x %.2x %.2x %.2x %.2x %.2x ", data[0],
			   data[1], data[2], data[3], data[4], data[5], data[6],
			   data[7]);
	}
}

int wait_for_backend_response(unsigned int *poll_var, unsigned int status,
			      spinlock_t * spin)
{
	int i = 0;
	int ret = 0;
	dprintk_in();
	if (!poll_var) {
		printk_err("Passing a null pointer to Poll on?\n");
		ret = 1;
		goto out;
	}
	do {
		//spin_lock(spin);
		if (*poll_var != status) {
			//spin_unlock(spin);
			break;
		}
		//spin_unlock(spin);
		i++;
		ndelay(1);
		if (i % 1000000 == 0)
			printk_inf("polling for %ds\n", (i - 1) / 1000);
		if (i > OMX_XEN_POLL_HARD_LIMIT) {
			printk_inf("timed out after %u us\n", (i - 1));
			ret = 1;
			goto out;
		}
	} while (1);
#if 0
	if (i > 0)
		printk_inf("polling for %dus\n", (i - 1));
#endif
out:
	dprintk_out();
	return ret;
}

/* Xen related stuff */
int
omx_poke_dom0(struct omx_xenfront_info *fe, int msg_id,
	      struct omx_xenif_request *ring_req)
{

	int notify;
	int err = 0;
	unsigned long flags;
	struct evtchn_send event;

	dprintk_in();

	spin_lock_irqsave(&fe->lock, flags);
	if (ring_req) {
		ring_req->func = msg_id;
		ring_req->id = 1;
	}
	wmb();
	switch (msg_id) {
	case OMX_CMD_XEN_DUMMY:
	case OMX_CMD_RECV_CONNECT_REPLY:
	case OMX_CMD_RECV_CONNECT_REQUEST:
	case OMX_CMD_RECV_RNDV:
	case OMX_CMD_RECV_NOTIFY:
	case OMX_CMD_RECV_LIBACK:
	case OMX_CMD_RECV_MEDIUM_FRAG:
	case OMX_CMD_RECV_SMALL:
	case OMX_CMD_RECV_TINY:{
			//RING_PUSH_REQUESTS(&(fe->recv_ring));
			RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&(fe->recv_ring), notify);
			dprintk_deb
			    ("after push: Poke dom0 with func = %#x, requests_produced_private= %d, requests_produced = %d\n",
			     msg_id, fe->recv_ring.req_prod_pvt,
			     fe->recv_ring.sring->req_prod);
			break;
		}
	default:{
			dprintk_deb
			    ("Poke dom0 with func = %#x, requests_produced_private= %d, requests_produced = %d\n",
			     msg_id, fe->ring.req_prod_pvt,
			     fe->ring.sring->req_prod);
			//RING_PUSH_REQUESTS(&(fe->ring));
			RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&(fe->ring), notify);
			break;
		}
	}

	if (notify) {
		event.port = fe->evtchn.local_port;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &event) != 0) {
			dprintk_deb("Failed to send event!\n");
			goto out;
		}
	}
out:
	spin_unlock_irqrestore(&fe->lock, flags);
	dprintk_out();
	return err;
}

void omx_xenif_interrupt_recv(struct work_struct *work)
{
	struct omx_xenfront_info *fe;
	struct omx_xenif_response *resp;
	struct omx_xenif_request *ring_req;
	//unsigned long flags;
	RING_IDX cons, prod;
	int more_to_do;
	uint32_t id;
	unsigned long bidx, idx;
	struct omx_xenif_front_ring *ring;

	dprintk_in();
	fe = container_of(work, struct omx_xenfront_info, msg_workq_task);

	/* dprintk_deb("ev_id %#lx omxbe=%#lx\n", (unsigned long)data, (unsigned long)fe); */
	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->recv_ring)) {
again_recv:
		dprintk_deb("responses_produced= %d, requests_produced = %d\n",
			    fe->recv_ring.sring->rsp_prod,
			    fe->recv_ring.sring->req_prod);
		dprintk_deb("RING_FREE_REQUESTS() = %#x, RING_FULL=%#x \n",
			    RING_FREE_REQUESTS((&fe->recv_ring)),
			    RING_FULL(&fe->recv_ring));
		ring = &fe->recv_ring;
		cons = fe->recv_ring.rsp_cons;
		prod = fe->recv_ring.sring->rsp_prod;
	} else
		goto out;

	rmb(); /* Ensure we see queued responses up to 'rp'. */
	while (cons != prod) {
		dprintk_deb("omx_xenif->ring.req_cons=%d, i=%d, rp=%d\n",
			    fe->ring.rsp_cons, fe->ring.rsp_cons,
			    fe->ring.sring->rsp_prod);
		dprintk_deb("omx_xenif->recv_ring.req_cons=%d, i=%d, rp=%d\n",
			    fe->recv_ring.rsp_cons, fe->recv_ring.rsp_cons,
			    fe->recv_ring.sring->rsp_prod);

		resp = RING_GET_RESPONSE(ring, cons++);

		id = resp->func;
		dprintk_deb
		    ("func =%#x, responses_produced= %d, requests_produced = %d\n",
		     resp->func, fe->ring.sring->rsp_prod,
		     fe->ring.sring->req_prod);

		switch (resp->func) {
		case OMX_CMD_XEN_RECV_PULL_DONE:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				uint32_t rid = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_RECV_PULL_DONE, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_pull_done));

				dump_xen_recv_pull_done(&resp->
							data.recv_pull_done);
				bidx = resp->data.recv_pull_done.board_index;
				idx = resp->data.recv_pull_done.eid;
				ret = resp->data.recv_pull_done.ret;
				rid = resp->data.recv_pull_done.rid;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				omx_notify_exp_event(endpoint,
						     &resp->data.
						     recv_pull_done.pull_done,
						     sizeof(struct
							    omx_evt_pull_done));
				//omx_xen_user_region_release(endpoint, rid);

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = OMX_CMD_XEN_DUMMY;
				omx_poke_dom0(endpoint->fe, OMX_CMD_XEN_DUMMY,
					      ring_req);

				break;
			}
		case OMX_CMD_RECV_PULL_REQUEST:{
				struct omx_endpoint *endpoint;
				struct omx_evt_recv_pull_request pull_request;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_PULL_REQUEST, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_recv_pull_request));

				dump_xen_recv_pull_request(&resp->
							   data.recv_pull_request);
				bidx = resp->data.recv_pull_request.board_index;
				idx = resp->data.recv_pull_request.eid;
				ret = resp->data.recv_pull_request.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				memcpy(&pull_request,
				       &resp->data.recv_pull_request.pull_req,
				       sizeof(pull_request));

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = OMX_CMD_XEN_DUMMY;
				omx_poke_dom0(endpoint->fe, OMX_CMD_XEN_DUMMY,
					      ring_req);
				break;
			}
		case OMX_CMD_RECV_MEDIUM_FRAG:
		case OMX_CMD_RECV_SMALL:
		case OMX_CMD_RECV_TINY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				unsigned long recvq_offset = 0;
				TIMER_START(&t2);
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_%#x, param=%lx\n",
				     resp->func,
				     sizeof(struct omx_cmd_xen_recv_msg));

				bidx = resp->data.recv_msg.board_index;
				idx = resp->data.recv_msg.eid;
				ret = resp->data.recv_msg.ret;
				recvq_offset = resp->data.recv_msg.recvq_offset;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = resp->func;
				if (resp->func == OMX_CMD_RECV_TINY) {
					/* notify the event */
					ret =
					    omx_notify_unexp_event(endpoint,
								   &resp->
								   data.recv_msg.
								   msg,
								   sizeof(struct
									  omx_evt_recv_msg));
				} else {

					dprintk_deb("%s: ret = %d, recvq=%#x\n",
						    __func__, ret,
						    recvq_offset);

					omx_commit_notify_unexp_event_with_recvq
					    (endpoint, &resp->data.recv_msg.msg,
					     sizeof(struct omx_evt_recv_msg));
				}

				dprintk_deb("%s: ret = %d, recvq=%#x\n",
					    __func__, ret, recvq_offset);

				omx_poke_dom0(endpoint->fe, resp->func,
					      ring_req);
				TIMER_STOP(&t2);

				break;
			}
		case OMX_CMD_RECV_LIBACK:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_LIBACK, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_liback));
				//dump_xen_recv_liback(&resp->data.recv_liback);

				bidx = resp->data.recv_liback.board_index;
				idx = resp->data.recv_liback.eid;
				ret = resp->data.recv_liback.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->
							   data.recv_liback.
							   liback,
							   sizeof(struct
								  omx_evt_recv_liback));

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = resp->func;

				omx_poke_dom0(endpoint->fe, resp->func,
					      ring_req);

				break;
			}
		case OMX_CMD_RECV_NOTIFY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_NOTIFY, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_msg));
				dump_xen_recv_notify(&resp->data.recv_msg);

				bidx = resp->data.recv_msg.board_index;
				idx = resp->data.recv_msg.eid;
				ret = resp->data.recv_msg.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->data.
							   recv_msg.msg,
							   sizeof(struct
								  omx_evt_recv_msg));

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = resp->func;
				omx_poke_dom0(endpoint->fe, resp->func,
					      ring_req);

				break;
			}
		case OMX_CMD_RECV_RNDV:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_RNDV, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_msg));

				dump_xen_recv_msg(&resp->data.recv_msg);
				bidx = resp->data.recv_msg.board_index;
				idx = resp->data.recv_msg.eid;
				ret = resp->data.recv_msg.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->data.
							   recv_msg.msg,
							   sizeof(struct
								  omx_evt_recv_msg));

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = resp->func;
				omx_poke_dom0(endpoint->fe, resp->func,
					      ring_req);

				break;
			}
		case OMX_CMD_RECV_CONNECT_REQUEST:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_CONNECT_REQUEST, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_recv_connect_request));
				dump_xen_recv_connect_request(&resp->
							      data.recv_connect_request);

				bidx =
				    resp->data.recv_connect_request.board_index;
				idx = resp->data.recv_connect_request.eid;
				ret = resp->data.recv_connect_request.ret;
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->
							   data.recv_connect_request.request,
							   sizeof(struct
								  omx_evt_recv_connect_request));

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = resp->func;
				omx_poke_dom0(endpoint->fe, resp->func,
					      ring_req);
				break;
			}
		case OMX_CMD_RECV_CONNECT_REPLY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_CONNECT_REPLY, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_recv_connect_request));
				dump_xen_recv_connect_reply(&resp->
							    data.recv_connect_reply);

				bidx =
				    resp->data.recv_connect_reply.board_index;
				idx = resp->data.recv_connect_reply.eid;
				ret = resp->data.recv_connect_reply.ret;
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->
							   data.recv_connect_reply.reply,
							   sizeof(struct
								  omx_evt_recv_connect_reply));

				dprintk_deb("%s: ret = %d\n", __func__, ret);
				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = resp->func;
				omx_poke_dom0(endpoint->fe, resp->func,
					      ring_req);
				break;
			}
			case OMX_CMD_XEN_SEND_MEDIUMSQ_DONE:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_SEND_MEDIUMSQ_FRAG_DONE, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_mediumsq_frag_done));

				bidx =
				    resp->data.send_mediumsq_frag_done.
				    board_index;
				idx = resp->data.send_mediumsq_frag_done.eid;
				ret = resp->data.send_mediumsq_frag_done.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				omx_notify_exp_event(endpoint,
						     &resp->data.
						     send_mediumsq_frag_done.sq_frag_done,
						     sizeof(struct
							    omx_evt_send_mediumsq_frag_done));

				ring_req =
				    RING_GET_REQUEST(ring,
						     ring->req_prod_pvt++);
				ring_req->func = OMX_CMD_XEN_DUMMY;
				omx_poke_dom0(endpoint->fe, OMX_CMD_XEN_DUMMY,
					      ring_req);

				break;
			}

		default:
			printk_err("Unknown event came in, %d\n", resp->func);
			dprintk_inf
			    ("resp_consumed=%d, responses_produced= %d, requests_produced = %d\n",
			     cons, fe->ring.sring->rsp_prod,
			     fe->ring.sring->req_prod);
			break;
		}
	}
	ring->rsp_cons = cons;
	wmb();

	RING_FINAL_CHECK_FOR_RESPONSES(&fe->recv_ring, more_to_do);
	if (more_to_do)
		goto again_recv;

#ifdef EXTRA_DEBUG_OMX
	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->recv_ring))
		printk_err
		    ("exiting, recv_although we have unconsumed responses, are you SURE?\n");
#endif

out:
	dprintk_out();
}

void omx_xenif_interrupt(struct work_struct *work)
{
	struct omx_xenfront_info *fe;
	struct omx_xenif_response *resp;
	//struct omx_xenif_request *ring_req;
	//unsigned long flags;
	RING_IDX cons, prod;
	int more_to_do;
	uint32_t id;
	unsigned long bidx, idx;
	struct omx_xenif_front_ring *ring;

	dprintk_in();
	fe = container_of(work, struct omx_xenfront_info, msg_workq_task);

	//spin_lock_irqsave(&fe->msg_handler_lock, flags);
	if (unlikely(fe->connected != OMXIF_STATE_CONNECTED)) {
		dprintk_warn("probably wrong variable, state disconnected\n");
		goto out;
	}
	/* dprintk_deb("ev_id %#lx omxbe=%#lx\n", (unsigned long)data, (unsigned long)fe); */

	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->ring)) {
again_send:
		dprintk_deb("responses_produced= %d, requests_produced = %d\n",
			    fe->ring.sring->rsp_prod, fe->ring.sring->req_prod);
		dprintk_deb("RING_FREE_REQUESTS() = %#x, RING_FULL=%#x \n",
			    RING_FREE_REQUESTS((&fe->ring)),
			    RING_FULL(&fe->ring));
		ring = &fe->ring;
		cons = fe->ring.rsp_cons;
		prod = fe->ring.sring->rsp_prod;
	} else
		goto out;

	rmb(); /* Ensure we see queued responses up to 'rp'. */
	while (cons != prod) {
		dprintk_deb("omx_xenif->ring.req_cons=%d, i=%d, rp=%d\n",
			    fe->ring.rsp_cons, fe->ring.rsp_cons,
			    fe->ring.sring->rsp_prod);
		dprintk_deb("omx_xenif->recv_ring.req_cons=%d, i=%d, rp=%d\n",
			    fe->recv_ring.rsp_cons, fe->recv_ring.rsp_cons,
			    fe->recv_ring.sring->rsp_prod);

		resp = RING_GET_RESPONSE(ring, cons++);

		id = resp->func;
		dprintk_deb
		    ("func =%#x, responses_produced= %d, requests_produced = %d\n",
		     resp->func, fe->ring.sring->rsp_prod,
		     fe->ring.sring->req_prod);

		switch (resp->func) {
		case OMX_CMD_SEND_MEDIUMSQ_FRAG:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_MEDIUMSQ_FRAG, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_mediumsq_frag));

				bidx =
				    resp->data.send_mediumsq_frag.board_index;
				idx = resp->data.send_mediumsq_frag.eid;
				ret = resp->data.send_mediumsq_frag.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_MEDIUMVA:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_MEDIUMVA, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_mediumva));

				bidx = resp->data.send_mediumva.board_index;
				idx = resp->data.send_mediumva.eid;
				ret = resp->data.send_mediumva.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				//      dump_xen_send_mediumva(&resp->data.send_mediumva);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_SMALL:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_SMALL, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_small));

				bidx = resp->data.send_small.board_index;
				idx = resp->data.send_small.eid;
				ret = resp->data.send_small.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				//      dump_xen_send_small(&resp->data.send_small);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_TINY:{
				struct omx_endpoint *endpoint;
				//struct omx_cmd_xen_send_tiny tiny;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_TINY, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_tiny));

				bidx = resp->data.send_tiny.board_index;
				idx = resp->data.send_tiny.eid;
				ret = resp->data.send_tiny.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				//      dump_xen_send_tiny(&resp->data.send_tiny);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}


		case OMX_CMD_RELEASE_UNEXP_SLOTS:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RELEASE_UNEXP_SLOTS, param=%lx\n",
				     sizeof(struct omx_ring_msg_endpoint));

				bidx = resp->data.endpoint.board_index;
				idx = resp->data.endpoint.endpoint_index;
				ret = resp->data.endpoint.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}

		case OMX_CMD_PULL:{
				struct omx_endpoint *endpoint;
				struct omx_cmd_xen_pull pull;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_PUL, param=%lx\n",
				     sizeof(struct omx_cmd_xen_pull));

				dump_xen_pull(&resp->data.pull);
				bidx = resp->data.pull.board_index;
				idx = resp->data.pull.eid;
				ret = resp->data.pull.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				memcpy(&pull, &resp->data.pull.pull,
				       sizeof(pull));

				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_NOTIFY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_NOTIFY, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_notify));

				bidx = resp->data.send_notify.board_index;
				idx = resp->data.send_notify.eid;
				ret = resp->data.send_notify.ret;

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				//dump_xen_send_notify(&resp->data.send_notify);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_RNDV:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_CONNECT_RNDV, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_connect_request));

				bidx = resp->data.send_rndv.board_index;
				idx = resp->data.send_rndv.eid;
				ret = resp->data.send_rndv.ret;
				dprintk_deb("bidx=%lu, idx=%lu\n", bidx, idx);
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_rndv(&resp->data.send_rndv);

				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				break;
			}
		case OMX_CMD_SEND_LIBACK:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_LIBACK, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_liback));

				bidx = resp->data.send_liback.board_index;
				idx = resp->data.send_liback.eid;
				ret = resp->data.send_liback.ret;
				dprintk_deb("bidx=%lu, idx=%lu\n", bidx, idx);
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_liback(&resp->data.send_liback);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				break;
			}
		case OMX_CMD_SEND_CONNECT_REQUEST:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_CONNECT_REQUEST, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_connect_request));

				bidx =
				    resp->data.send_connect_request.board_index;
				idx = resp->data.send_connect_request.eid;
				ret = resp->data.send_connect_request.ret;
				dprintk_deb("bidx=%lu, idx=%lu\n", bidx, idx);
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_connect_request(&resp->
							      data.send_connect_request);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				break;
			}
		case OMX_CMD_SEND_CONNECT_REPLY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_CONNECT_REPLY, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_connect_reply));

				bidx =
				    resp->data.send_connect_reply.board_index;
				idx = resp->data.send_connect_reply.eid;
				ret = resp->data.send_connect_reply.ret;
				dprintk_deb("bidx=%lu, idx=%lu\n", bidx, idx);
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_connect_reply(&resp->
							    data.send_connect_reply);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				break;
			}
		case OMX_CMD_PEER_FROM_INDEX:
		case OMX_CMD_PEER_FROM_ADDR:
		case OMX_CMD_PEER_FROM_HOSTNAME:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_GET_PEER_%#lx, param=%lx\n",
				     (unsigned long)resp->func,
				     sizeof(struct omx_cmd_xen_misc_peer_info));

				ret = resp->data.mpi.ret;
				//dump_xen_misc_peer_info(&resp->data.mpi);
				spin_lock(&fe->status_lock);
				if (!ret) {
					memcpy(&fe->peer_info,
					       &resp->data.mpi.info,
					       sizeof(struct
						      omx_cmd_misc_peer_info));
					memcpy(fe->peer_info.hostname,
					       resp->data.mpi.info.hostname,
					       sizeof(resp->data.mpi.info.
						      hostname));
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				} else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);
				break;
			}
		case OMX_CMD_GET_ENDPOINT_INFO:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_GET_ENDPOINT_INFO, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_get_endpoint_info));

				bidx = resp->data.gei.board_index;
				idx = resp->data.gei.eid;
				ret = resp->data.gei.ret;
				dprintk_deb("bidx=%lu, idx=%lu\n", bidx, idx);
				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				memcpy(&endpoint->endpoint_info,
				       &resp->data.gei.info,
				       sizeof(struct omx_endpoint_info));
				dump_xen_get_endpoint_info(&resp->data.gei);
				endpoint->info_status =
				    OMX_ENDPOINT_STATUS_DONE;

				dprintk_deb
				    ("board %#lx, endpoint %#lx gave us endpoint info!\n",
				     bidx, idx);

				break;
			}
		case OMX_CMD_XEN_GET_BOARD_COUNT:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_GET_BOARD_COUNT, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_get_board_count));

				ret = resp->data.gbc.ret;

				fe->board_count = resp->data.gbc.board_count;

				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				break;

			}
		case OMX_CMD_XEN_PEER_TABLE_GET_STATE:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_PEER_TABLE_GET_STATE, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_peer_table_get_state));

				bidx = resp->data.ptgs.board_index;
				ret = resp->data.ptgs.ret;

				memcpy(&fe->state, &resp->data.ptgs.state,
				       sizeof(struct omx_cmd_peer_table_state));

				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				break;

			}
		case OMX_CMD_GET_BOARD_INFO:{
				//struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_GET_BOARD_INFO, param=%lx\n",
				     sizeof(struct omx_cmd_xen_get_board_info));

				bidx = resp->data.gbi.board_index;
				idx = resp->data.gbi.eid;
				ret = resp->data.gbi.ret;

				dprintk_deb("board_addr = %#llx\n",
					    resp->data.gbi.info.addr);
				memcpy(&fe->board_info, &resp->data.gbi.info,
				       sizeof(struct omx_board_info));
				dprintk_deb("board_addr = %llx\n",
					    fe->board_info.addr);
				dump_xen_get_board_info(&resp->data.gbi);
				spin_lock(&fe->status_lock);
				if (!ret)
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->status =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				spin_unlock(&fe->status_lock);

				dprintk_deb
				    ("board %#lx, endpoint %#lx gave us board info, ret = %d!\n",
				     bidx, idx, ret);

				break;

			}
		case OMX_CMD_XEN_OPEN_ENDPOINT:
			{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_OPEN_ENDPOINT, param=%lx\n",
				     sizeof(struct omx_ring_msg_endpoint));
				bidx = resp->data.endpoint.board_index;
				idx = resp->data.endpoint.endpoint_index;
				ret = resp->data.endpoint.ret;
				dprintk_deb
				    ("board %#lx, endpoint %#lx, backend ret=%d\n",
				     bidx, idx, ret);

				endpoint = fe->endpoints[idx];
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					//break;
				}
				//endpoint = resp->data.endpoint.endpoint;
				if (ret || !endpoint) {
					printk_err
					    ("endpoint %#lx, is not READY (ret = %d, closing)\n",
					     idx, ret);
					endpoint->status = OMX_ENDPOINT_STATUS_CLOSING;	/* FIXME */
					omx_endpoint_close(endpoint, 0);
					//goto out;
					break;
				}
				dump_xen_ring_msg_endpoint(&resp->
							   data.endpoint);
				endpoint->status = OMX_ENDPOINT_STATUS_OK;	/* FIXME */

				dprintk_deb
				    ("board %#lx, endpoint %#lx is READY\n",
				     bidx, idx);

				break;
			}
		case OMX_CMD_XEN_CLOSE_ENDPOINT:
			{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_CLOSE_ENDPOINT, param=%lx\n",
				     sizeof(struct omx_ring_msg_endpoint));
				bidx = resp->data.endpoint.board_index;
				idx = resp->data.endpoint.endpoint_index;
				ret = resp->data.endpoint.ret;
				endpoint = fe->endpoints[idx];
				if (ret || !endpoint) {
					printk_err
					    ("endpoint %#lx, is not READY (ret = %d, closing)\n",
					     idx, ret);
					endpoint->status = OMX_ENDPOINT_STATUS_CLOSING;	/* FIXME */
					omx_endpoint_close(endpoint, 0);
					//goto out;
					break;
				}
				dump_xen_ring_msg_endpoint(&resp->
							   data.endpoint);
				endpoint->status = OMX_ENDPOINT_STATUS_OK;
				dprintk_deb
				    ("board %#lx, endpoint %#lx is CLOSED\n",
				     bidx, idx);
				break;
			}
		case OMX_CMD_XEN_CREATE_USER_REGION:
			{
				struct omx_endpoint *endpoint;
				struct omx_user_region *region;
				uint32_t eid, id;
				int status;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_CREATE_USER_REGION, param=%lx\n",
				     sizeof(struct
					    omx_ring_msg_create_user_region));
				eid = resp->data.cur.eid;
				id = resp->data.cur.id;
				status = resp->data.cur.status;
				endpoint = fe->endpoints[eid];
				spin_lock(&endpoint->user_regions_lock);
				region =
				    rcu_dereference_protected
				    (endpoint->user_regions[id], 1);
				dprintk_deb
				    ("Region is created for endpoint idx=%d (@%#lx), region = %#lx "
				     "status = %d\n",
				     eid, (unsigned long)endpoint,
				     (unsigned long)region, region->status);
				spin_unlock(&endpoint->user_regions_lock);
				dump_xen_ring_msg_create_user_region
				    (&resp->data.cur);
				if (!region) {printk_err("CREATE_region is NULL!\n"); break;}
				spin_lock(&region->status_lock);
				if (status) {
					dprintk_deb
					    ("Failed to deregister user region%d, will now abort\n",
					     id);
					region->status =
					    OMX_USER_REGION_STATUS_FAILED;
				} else
					region->status =
					    OMX_USER_REGION_STATUS_REGISTERED;
					endpoint->special_status =
					    OMX_USER_REGION_STATUS_REGISTERED;
				spin_unlock(&region->status_lock);

				break;
			}
		case OMX_CMD_XEN_DESTROY_USER_REGION:
			{
				struct omx_endpoint *endpoint;
				struct omx_user_region *region;
				uint32_t eid, id;
				uint8_t status;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_DESTROY_USER_REGION, param=%lx\n",
				     sizeof(struct
					    omx_ring_msg_destroy_user_region));
				eid = resp->data.dur.eid;
				id = resp->data.dur.id;
				status = resp->data.dur.status;
				//endpoint = fe->endpoints[eid];
				//spin_lock(&endpoint->user_regions_lock);
				region = (struct omx_user_region *) resp->data.dur.region;

				if (unlikely(!region)) {
					printk(KERN_ERR "%s: %d\n", __func__,
					       id);
					spin_unlock
					    (&endpoint->user_regions_lock);
					goto out;
				}
				//dprintk_inf("region = %p\n", (void*) region);
				//spin_unlock(&endpoint->user_regions_lock);
				//dump_xen_ring_msg_destroy_user_region (&resp->data.dur);
				//spin_lock(&region->status_lock);
				if (region) {
					if (status) {
						//region->granted = 0;
						region->status =
						    OMX_USER_REGION_STATUS_DEREGISTERED;
					}
					else {
						region->status =
						    OMX_USER_REGION_STATUS_FAILED;
					}
				} else {
					printk_err("region pointer invalid!\n");
				}

				//spin_unlock(&region->status_lock);
				break;
			}
		default:
			printk_err("Unknown event came in, %d\n", resp->func);
			dprintk_inf
			    ("resp_consumed=%d, responses_produced= %d, requests_produced = %d\n",
			     cons, fe->ring.sring->rsp_prod,
			     fe->ring.sring->req_prod);
			break;
		}
	}
	ring->rsp_cons = cons;
	wmb();

#if 0
	RING_FINAL_CHECK_FOR_RESPONSES(&fe->recv_ring, more_to_do);
	if (more_to_do)
		goto again_recv;
#endif

	RING_FINAL_CHECK_FOR_RESPONSES(&fe->ring, more_to_do);
	if (more_to_do)
		goto again_send;

#ifdef EXTRA_DEBUG_OMX
	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->ring))
		printk_err
		    ("exiting, although we have unconsumed responses, are you SURE?\n");
#endif


#if 0
	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->recv_ring))
		printk_err
		    ("exiting, recv_although we have unconsumed responses, are you SURE?\n");
#endif

out:
	//spin_unlock_irqrestore(&fe->msg_handler_lock, flags);
	dprintk_out();
}

int omx_xen_iface_get_info(uint32_t board_index, struct omx_board_info *info)
{
	int ret = 0;

	dprintk_in();

	info->drivername[0] = '\0';

	rcu_read_lock();
	/* It's a Xen iface ;-) */

	info->addr = 0;
	info->numa_node = -1;
	strncpy(info->ifacename, "fake", OMX_IF_NAMESIZE);
	info->ifacename[OMX_IF_NAMESIZE - 1] = '\0';
	strncpy(info->hostname, "Xen Communication", OMX_HOSTNAMELEN_MAX);
	info->hostname[OMX_HOSTNAMELEN_MAX - 1] = '\0';

	rcu_read_unlock();
	goto out;

out:
	dprintk_out();
	return ret;

}

int omx_xen_ifaces_get_count(uint32_t * count)
{
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_XEN_GET_BOARD_COUNT;
	//ring_req->data.ptgs.board_index = 0;
	omx_poke_dom0(fe, OMX_CMD_XEN_GET_BOARD_COUNT, ring_req);

	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_FREE); */
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}

	*count = fe->board_count;

out:
	dprintk_out();
	return ret;
}

int omx_xen_peer_table_state(struct omx_cmd_peer_table_state *state)
{
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_XEN_PEER_TABLE_GET_STATE;
	ring_req->data.ptgs.board_index = 0;
	omx_poke_dom0(fe, OMX_CMD_XEN_PEER_TABLE_GET_STATE, ring_req);

	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_FREE); */
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (fe->status == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}
	memcpy(state, &fe->state, sizeof(*state));

out:
	dprintk_out();
	return ret;
}

int omx_ioctl_xen_get_board_info(struct omx_endpoint *endpoint,
				 void __user * uparam)
{
	struct omx_cmd_get_board_info get_board_info;
	struct omx_xenfront_info *fe;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();
#if 1
	/*
	 * the endpoint is already acquired by the file,
	 * just check its status
	 */
	if (endpoint->status != OMX_ENDPOINT_STATUS_OK) {
		/* the endpoint is not open, get the command parameter and use its board_index */
		ret = copy_from_user(&get_board_info, (void __user *)uparam,
				     sizeof(get_board_info));
		if (unlikely(ret != 0)) {
			ret = -EFAULT;
			printk(KERN_ERR
			       "Open-MX: Failed to read get_board_info command argument, error %d\n",
			       ret);
			goto out;
		}
	} else {
		/* endpoint acquired, use its board index */
		get_board_info.board_index = endpoint->board_index;
	}
#endif
	/* FIXME!!!! */

//      get_board_info.board_index = 0;
	fe = endpoint->fe;

	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_GET_BOARD_INFO;
	ring_req->data.gbi.board_index = get_board_info.board_index;
	ring_req->data.gbi.eid = endpoint->endpoint_index;
	dump_xen_get_board_info(&ring_req->data.gbi);
	omx_poke_dom0(endpoint->fe, OMX_CMD_GET_BOARD_INFO, ring_req);
	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_FREE); */
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	memcpy(&get_board_info.info, &fe->board_info,
	       sizeof(struct omx_board_info));

	if (fe->status == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}
	dprintk_deb("ret =%d\n", ret);
	dprintk_deb("board_addr = %#llx, ret = %d\n", get_board_info.info.addr,
		    ret);

	ret = copy_to_user((void __user *)uparam, &get_board_info,
			   sizeof(get_board_info));
	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		printk(KERN_ERR
		       "Open-MX: Failed to write get_board_info command result, error %d\n",
		       ret);
		goto out;
	}

out:
	dprintk_out();
	return ret;
}

/*
 * Return some info about an endpoint.
 */
int
omx_xen_endpoint_get_info(uint32_t board_index, uint32_t endpoint_index,
			  struct omx_endpoint_info *info)
{

	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	struct omx_endpoint *endpoint;
	int ret;

	dprintk_in();
	dprintk_deb("bidx = %#lx, idx = %#lx\n", (unsigned long)board_index,
		    (unsigned long)endpoint_index);
	endpoint = fe->endpoints[endpoint_index];
	BUG_ON(!endpoint);

	spin_lock(&endpoint->status_lock);
	endpoint->info_status = OMX_ENDPOINT_STATUS_DOING;
	spin_unlock(&endpoint->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = OMX_CMD_GET_ENDPOINT_INFO;
	ring_req->data.gei.board_index = endpoint->board_index;
	ring_req->data.gei.eid = endpoint->endpoint_index;
	dump_xen_get_endpoint_info(&ring_req->data.gei);
	omx_poke_dom0(endpoint->fe, OMX_CMD_GET_ENDPOINT_INFO, ring_req);
	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_DONE); */
	if (wait_for_backend_response
	    (&endpoint->info_status, OMX_ENDPOINT_STATUS_DOING,
	     &endpoint->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	memcpy(info, &endpoint->endpoint_info,
	       sizeof(struct omx_endpoint_info));
out:
	dprintk_out();
	return ret;
}

/*
 * Lookup board_addr and/or hostname by index.
 *
 * board_addr and hostname may be NULL.
 *
 * Cannot be called by BH.
 */
int
omx_xen_peer_lookup(uint32_t * index,
		    uint64_t * board_addr, char *hostname, uint32_t cmd)
{
	//struct omx_peer *peer;
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	int ret = 0;

	dprintk_in();
	BUG_ON(!fe);
	spin_lock(&fe->status_lock);
	fe->status = OMX_XEN_FRONTEND_STATUS_DOING;
	spin_unlock(&fe->status_lock);
	ring_req = RING_GET_REQUEST(&(fe->ring), fe->ring.req_prod_pvt++);
	ring_req->func = cmd;
	if (cmd == OMX_CMD_PEER_FROM_INDEX) {
		if (index)
			ring_req->data.mpi.info.index = *index;
		else
			printk_err("Index is NULL!!!\n");
	} else if (cmd == OMX_CMD_PEER_FROM_ADDR) {
		dprintk_deb("Peer from addr\n");
		if (board_addr)
			memcpy(&ring_req->data.mpi.info.board_addr, board_addr,
			       sizeof(uint64_t));
		else
			printk_err("board address is NULL!!!\n");
	} else if (cmd == OMX_CMD_PEER_FROM_HOSTNAME) {
		if (hostname)
			memcpy(ring_req->data.mpi.info.hostname, hostname,
			       OMX_HOSTNAMELEN_MAX);
		else
			printk_err("hostname is NULL!!!\n");
	}

	dump_xen_misc_peer_info(&ring_req->data.mpi);
	omx_poke_dom0(fe, cmd, ring_req);
	if (wait_for_backend_response
	    (&fe->status, OMX_XEN_FRONTEND_STATUS_DOING, &fe->status_lock)) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}

	if (cmd == OMX_CMD_PEER_FROM_INDEX) {
		if (board_addr)
			memcpy(board_addr, &fe->peer_info.board_addr,
			       sizeof(uint64_t));
		if (hostname)
			memcpy(hostname, fe->peer_info.hostname,
			       OMX_HOSTNAMELEN_MAX);
	} else if (cmd == OMX_CMD_PEER_FROM_ADDR) {
		if (index)
			*index = fe->peer_info.index;
		if (hostname)
			memcpy(hostname, fe->peer_info.hostname,
			       OMX_HOSTNAMELEN_MAX);
	} else if (cmd == OMX_CMD_PEER_FROM_HOSTNAME) {
		if (board_addr)
			memcpy(board_addr, &fe->peer_info.board_addr,
			       sizeof(uint64_t));
		if (index)
			*index = fe->peer_info.index;
	}
	dprintk_deb("board=%#llx\n", *board_addr);
	dprintk_deb("index =%#lx\n", (unsigned long)*index);
	dprintk_deb("hostname=%s\n", hostname);
	if (fe->status == OMX_XEN_FRONTEND_STATUS_FAILED)
		ret = -EINVAL;
	dprintk_deb("ret =%d\n", ret);

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
