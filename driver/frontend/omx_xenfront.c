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


#define OMX_XEN_DELAY	1
#define OMX_XEN_POLL_HARD_LIMIT OMX_XEN_DELAY * 1000 * 1000 * 1000 // wait for 1s
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xen_lib.h"
#include "omx_xenfront.h"
#include "omx_xenfront_endpoint.h"
#include "omx_xenfront_reg.h"

timers_t t_poke_dom0;
timers_t t_recv_rndv, t_recv_medsmall, t_recv_tiny, t_recv_connect_request,
    t_recv_connect_reply, t_recv_liback, t_recv_notify, t_pull_request,
    t_pull_done, t_recv_mediumsq;

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

/*
 * We keep track of each request to handle backend's notifications and release
 * IOCTL calls from user-space.
 *
 * WARNING: We can handle up to OMX_MAX_INFLIGHT_REQUESTS at the same time.
 */
struct omx_xenif_request *omx_ring_get_request(struct omx_xenfront_info
						      *fe)
{
	struct omx_xenif_request *ring_req;
	dprintk_in();

	ring_req = RING_GET_REQUEST(&fe->ring, fe->ring.req_prod_pvt++);
	fe->requests[(fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS] =
	    OMX_XEN_FRONTEND_STATUS_DOING;

	dprintk_out();
	return ring_req;
}

int wait_for_backend_response(unsigned int *poll_var, unsigned int status,
			      spinlock_t * spin)
{
	int i = 0;
	int ret = 0;
	dprintk_in();
	if (!poll_var) {
		printk_err("Passing a null pointer to Poll on?\n");
		ret = -EINVAL;
		goto out;
	}
	do {
		if (*poll_var != status) {
			ret = i;
			break;
		}
		ndelay(OMX_XEN_DELAY);
		i++;
#if 0
		if (i % 1000000 == 0)
			printk_inf("polling for %dms\n", (i - 1) / 1000);
#endif
		if (i > OMX_XEN_POLL_HARD_LIMIT) {
			printk_inf("timed out after %u ns\n", (i - 1) / OMX_XEN_DELAY);
			ret = -EINVAL;
			goto out;
		}
	} while (1);
out:
	dprintk_out();
	return ret;
}

/* Xen related stuff */
int
omx_poke_dom0(struct omx_xenfront_info *fe, struct omx_xenif_request *ring_req)
{

	int notify;
	int err = 0;
	unsigned long flags;
	struct evtchn_send event;
	struct omx_xenif_front_ring *ring;

	dprintk_in();

	TIMER_START(&t_poke_dom0);
	spin_lock_irqsave(&fe->lock, flags);
	if (unlikely(!ring_req)) {
		/* If our ring buffer is null, then we fail ungracefully */
		printk_err("Null ring_resp\n");
		err = -EINVAL;
		goto out;
	}

	/* Choose the ring to shovel the request */
	switch (ring_req->func) {
	case OMX_CMD_XEN_DUMMY:
	case OMX_CMD_RECV_CONNECT_REPLY:
	case OMX_CMD_RECV_CONNECT_REQUEST:
	case OMX_CMD_RECV_RNDV:
	case OMX_CMD_RECV_NOTIFY:
	case OMX_CMD_RECV_LIBACK:
	case OMX_CMD_RECV_MEDIUM_FRAG:
	case OMX_CMD_RECV_SMALL:
	case OMX_CMD_RECV_TINY:{
			ring = &fe->recv_ring;
			break;
		}
	default:{
			ring = &fe->ring;
			break;
		}
	}
	//RING_PUSH_REQUESTS(&(fe->recv_ring));
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(ring, notify);
	dprintk_deb
	    ("after push: Poke dom0 with func = %#x, requests_produced_private= %d, "
	     "requests_produced = %d\n",
	     ring_req->func, ring->req_prod_pvt, ring->sring->req_prod);

	if (notify) {
		event.port = fe->evtchn.local_port;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_send, &event) != 0) {
			dprintk_deb("Failed to send event!\n");
			goto out;
		}
	}
out:
	spin_unlock_irqrestore(&fe->lock, flags);
	TIMER_STOP(&t_poke_dom0);
	dprintk_out();
	return err;
}

/*
 * Will account for an elegant solution to be notified by the backend
 */
#if 0
static struct omx_xenif_request *omx_xenfront_get_request(struct omx_xenfront_info *fe,
						     struct omx_xenif_response
						     *resp)
{
	uint32_t request_id;
	struct omx_xenif_request *request = NULL;

	dprintk_in();

	request_id = resp->request_id;
	request = fe->request_list[request_id];

	dprintk_deb("got req_id = %d, @%p\n", request_id, request);

	dprintk_out();
	return request;
}
#endif

static struct omx_endpoint *omx_xenfront_get_endpoint(struct omx_xenfront_info *fe,
						     struct omx_xenif_response
						     *resp)
{
	uint32_t bi, eid;
	struct omx_endpoint *endpoint = NULL;

	dprintk_in();

	bi = resp->board_index;
	eid = resp->eid;
	endpoint = fe->endpoints[eid];

	dprintk_deb("got (%d,%d)\n", bi, eid);

	dprintk_out();
	return endpoint;
}

static void omx_xenfront_ack(struct omx_endpoint *endpoint, uint32_t func)
{
	struct omx_xenfront_info *fe = endpoint->fe;
	struct omx_xenif_front_ring *ring = &fe->recv_ring;
	struct omx_xenif_request *ring_req;
	dprintk_in();

	ring_req =
	    RING_GET_REQUEST(ring,
			     ring->req_prod_pvt++);
	ring_req->func = func;
	omx_poke_dom0(endpoint->fe, ring_req);

	dprintk_out();

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

				//dump_xen_recv_pull_done(&resp->data.recv_pull_done);
				endpoint = omx_xenfront_get_endpoint(fe, resp);

				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				rid = resp->data.recv_pull_done.rid;

				TIMER_START(&t_pull_done);
				omx_notify_exp_event(endpoint,
						     &resp->data.
						     recv_pull_done.pull_done,
						     sizeof(struct
							    omx_evt_pull_done));
				//omx_xen_user_region_release(endpoint, rid);
				TIMER_STOP(&t_pull_done);

				omx_xenfront_ack(endpoint, OMX_CMD_XEN_DUMMY);
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

				//dump_xen_recv_pull_request(&resp-> data.recv_pull_request);
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_pull_request);
				memcpy(&pull_request,
				       &resp->data.recv_pull_request.pull_req,
				       sizeof(pull_request));
				TIMER_STOP(&t_pull_request);

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				omx_xenfront_ack(endpoint, OMX_CMD_XEN_DUMMY);
				break;
			}
		case OMX_CMD_RECV_MEDIUM_FRAG:
		case OMX_CMD_RECV_SMALL:
		case OMX_CMD_RECV_TINY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_%#x, param=%lx\n",
				     resp->func,
				     sizeof(struct omx_cmd_xen_recv_msg));

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				if (resp->func == OMX_CMD_RECV_TINY) {
					TIMER_STOP(&endpoint->otherway);
					TIMER_START(&t_recv_tiny);
					/* notify the event */
					ret =
					    omx_notify_unexp_event(endpoint,
								   &resp->
								   data.recv_msg.
								   msg,
								   sizeof(struct
									  omx_evt_recv_msg));
					TIMER_STOP(&t_recv_tiny);
				} else {

					dprintk_deb("%s: ret = %d\n",
						    __func__, ret);

					TIMER_START(&t_recv_medsmall);
					omx_commit_notify_unexp_event_with_recvq
					    (endpoint, &resp->data.recv_msg.msg,
					     sizeof(struct omx_evt_recv_msg));
					TIMER_STOP(&t_recv_medsmall);
				}

#if 0
				dprintk_deb("%s: ret = %d, recvq=%#x\n",
					    __func__, ret, recvq_offset);
#endif

				omx_xenfront_ack(endpoint, resp->func);


				break;
			}
		case OMX_CMD_RECV_LIBACK:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_LIBACK, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_liback));
				//dump_xen_recv_liback(&resp->data.recv_liback);

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_recv_liback);
				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->
							   data.recv_liback.
							   liback,
							   sizeof(struct
								  omx_evt_recv_liback));
				TIMER_STOP(&t_recv_liback);

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				omx_xenfront_ack(endpoint, resp->func);

				break;
			}
		case OMX_CMD_RECV_NOTIFY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_NOTIFY, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_msg));
				dump_xen_recv_notify(&resp->data.recv_msg);

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_recv_notify);
				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->data.
							   recv_msg.msg,
							   sizeof(struct
								  omx_evt_recv_msg));
				TIMER_STOP(&t_recv_notify);

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				omx_xenfront_ack(endpoint, resp->func);

				break;
			}
		case OMX_CMD_RECV_RNDV:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_RECV_RNDV, param=%lx\n",
				     sizeof(struct omx_cmd_xen_recv_msg));

				dump_xen_recv_msg(&resp->data.recv_msg);
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_recv_rndv);
				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->data.
							   recv_msg.msg,
							   sizeof(struct
								  omx_evt_recv_msg));
				TIMER_STOP(&t_recv_rndv);

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				omx_xenfront_ack(endpoint, resp->func);

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

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_recv_connect_request);
				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->
							   data.recv_connect_request.request,
							   sizeof(struct
								  omx_evt_recv_connect_request));
				TIMER_STOP(&t_recv_connect_request);

				dprintk_deb("%s: ret = %d\n", __func__, ret);

				omx_xenfront_ack(endpoint, resp->func);
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
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_recv_connect_reply);
				/* notify the event */
				ret =
				    omx_notify_unexp_event(endpoint,
							   &resp->
							   data.recv_connect_reply.reply,
							   sizeof(struct
								  omx_evt_recv_connect_reply));
				TIMER_STOP(&t_recv_connect_reply);

				dprintk_deb("%s: ret = %d\n", __func__, ret);
				omx_xenfront_ack(endpoint, resp->func);
				break;
			}
			case OMX_CMD_XEN_SEND_MEDIUMSQ_DONE:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_SEND_MEDIUMSQ_FRAG_DONE, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_mediumsq_frag_done));

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				TIMER_START(&t_recv_mediumsq);
				omx_notify_exp_event(endpoint,
						     &resp->data.
						     send_mediumsq_frag_done.sq_frag_done,
						     sizeof(struct
							    omx_evt_send_mediumsq_frag_done));
				TIMER_STOP(&t_recv_mediumsq);

				omx_xenfront_ack(endpoint, OMX_CMD_XEN_DUMMY);

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
		dprintk_deb("probably wrong variable, state disconnected\n");
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

				ret = resp->ret;

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_MEDIUMVA:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_MEDIUMVA, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_mediumva));

				ret = resp->ret;

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				//      dump_xen_send_mediumva(&resp->data.send_mediumva);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_SMALL:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_SMALL, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_small));

				ret = resp->ret;

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				//      dump_xen_send_small(&resp->data.send_small);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
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

				ret = resp->ret;

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				//      dump_xen_send_tiny(&resp->data.send_tiny);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
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
				ret = resp->ret;

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				memcpy(&pull, &resp->data.pull.pull,
				       sizeof(pull));

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_NOTIFY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_NOTIFY, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_notify));

				ret = resp->ret;

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}

				//dump_xen_send_notify(&resp->data.send_notify);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				dprintk_deb("%s: ret = %d\n", __func__, ret);

				break;
			}
		case OMX_CMD_SEND_RNDV:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_RNDV, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_connect_request));

				ret = resp->ret;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_rndv(&resp->data.send_rndv);

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

				break;
			}
		case OMX_CMD_SEND_LIBACK:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				uint32_t request_id = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_LIBACK, param=%lx\n",
				     sizeof(struct omx_cmd_xen_send_liback));

				ret = resp->ret;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_liback(&resp->data.send_liback);
				request_id = resp->request_id;

				if (!ret)
					fe->requests[request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				break;
			}
		case OMX_CMD_SEND_CONNECT_REQUEST:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_CONNECT_REQUEST, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_connect_request));

				ret = resp->ret;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_connect_request(&resp->
							      data.send_connect_request);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

				break;
			}
		case OMX_CMD_SEND_CONNECT_REPLY:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_SEND_CONNECT_REPLY, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_send_connect_reply));

				ret = resp->ret;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					break;
				}
				dump_xen_send_connect_reply(&resp->
							    data.send_connect_reply);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

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

				ret = resp->ret;
				//dump_xen_misc_peer_info(&resp->data.mpi);
				if (!ret) {
					memcpy(&fe->peer_info,
					       &resp->data.mpi.info,
					       sizeof(struct
						      omx_cmd_misc_peer_info));
					memcpy(fe->peer_info.hostname,
					       resp->data.mpi.info.hostname,
					       sizeof(resp->data.mpi.info.
						      hostname));
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				} else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;
				break;
			}
		case OMX_CMD_GET_ENDPOINT_INFO:{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_GET_ENDPOINT_INFO, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_get_endpoint_info));

				ret = resp->ret;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
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

				break;
			}
		case OMX_CMD_XEN_GET_BOARD_COUNT:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_GET_BOARD_COUNT, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_get_board_count));

				ret = resp->ret;

				fe->board_count = resp->data.gbc.board_count;

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

				break;

			}
		case OMX_CMD_XEN_PEER_TABLE_SET_STATE:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_PEER_TABLE_SET_STATE, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_peer_table_state));

				bidx = resp->board_index;
				ret = resp->ret;

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

				break;

			}
		case OMX_CMD_XEN_PEER_TABLE_GET_STATE:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_PEER_TABLE_GET_STATE, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_peer_table_state));

				bidx = resp->board_index;
				ret = resp->ret;

				memcpy(&fe->state, &resp->data.pts.state,
				       sizeof(struct omx_cmd_peer_table_state));

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

				break;

			}
		case OMX_CMD_XEN_SET_HOSTNAME:{
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_SET_HOSTNAME, param=%lx\n",
				     sizeof(struct
					    omx_cmd_xen_set_hostname));

				bidx = resp->board_index;
				ret = resp->ret;

				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

				break;

			}
		case OMX_CMD_GET_BOARD_INFO:{
				//struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_GET_BOARD_INFO, param=%lx\n",
				     sizeof(struct omx_cmd_xen_get_board_info));

				bidx = resp->board_index;
				idx = resp->eid;
				ret = resp->ret;

				dprintk_deb("board_addr = %#llx\n",
					    resp->data.gbi.info.addr);
				memcpy(&fe->board_info, &resp->data.gbi.info,
				       sizeof(struct omx_board_info));
				dprintk_deb("board_addr = %llx\n",
					    fe->board_info.addr);
				dump_xen_get_board_info(&resp->data.gbi);
				if (!ret)
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_DONE;
				else
					fe->requests[resp->request_id] =
					    OMX_XEN_FRONTEND_STATUS_FAILED;

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
				ret = resp->ret;
				dprintk_deb
				    ("board %#lx, endpoint %#lx, backend ret=%d\n",
				     bidx, idx, ret);

				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err
					    ("Endpoint is null:S, ret = %d\n",
					     ret);
					//goto out;
					fe->requests[resp->request_id] = OMX_XEN_FRONTEND_STATUS_FAILED;
					break;
				}
				dump_xen_ring_msg_endpoint(&resp->
							   data.endpoint);

				fe->requests[resp->request_id] = OMX_XEN_FRONTEND_STATUS_DONE;
				break;
			}
		case OMX_CMD_XEN_CLOSE_ENDPOINT:
			{
				struct omx_endpoint *endpoint;
				int16_t ret = 0;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_CLOSE_ENDPOINT, param=%lx\n",
				     sizeof(struct omx_ring_msg_endpoint));
				ret = resp->ret;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (ret || !endpoint) {
					printk_err
					    ("endpoint id=%u is not READY (ret = %d, closing)\n",
					     resp->eid, ret);
					fe->requests[resp->request_id] = OMX_XEN_FRONTEND_STATUS_FAILED;
					//goto out;
					break;
				}
				dump_xen_ring_msg_endpoint(&resp->
							   data.endpoint);
				endpoint->status = OMX_ENDPOINT_STATUS_OK;
				fe->requests[resp->request_id] = OMX_XEN_FRONTEND_STATUS_DONE;
				break;
			}
		case OMX_CMD_XEN_CREATE_USER_REGION:
			{
				struct omx_endpoint *endpoint;
				struct omx_user_region *region;
				uint32_t eid, id;
				uint32_t request_id;
				int status;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_CREATE_USER_REGION, param=%lx\n",
				     sizeof(struct
					    omx_ring_msg_create_user_region));
				id = resp->data.cur.id;
				status = resp->data.cur.status;
				request_id = resp->request_id;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err("endpoint is NULL!!\n");
					break;
				}
				spin_lock(&endpoint->user_regions_lock);
				region =
				    rcu_dereference_protected
				    (endpoint->user_regions[id], 1);
				dprintk_deb
				    ("Region is created for endpoint (@%#lx), region = %#lx "
				     "status = %d\n",
				     (unsigned long)endpoint,
				     (unsigned long)region, region->status);
				spin_unlock(&endpoint->user_regions_lock);
				dump_xen_ring_msg_create_user_region
				    (&resp->data.cur);
				if (!region) {printk_err("CREATE_region is NULL!\n"); break;}
				spin_lock(&region->status_lock);
				if (status) {
					printk_err
					    ("Failed to register user region%d\n",
					     id);
					fe->requests[request_id] =
					    OMX_USER_REGION_STATUS_FAILED;
				} else
					fe->requests[request_id] =
					    OMX_USER_REGION_STATUS_REGISTERED;
#ifdef OMX_XEN_FE_SHORTCUT
					endpoint->special_status_reg =
					    OMX_USER_REGION_STATUS_REGISTERED;
#endif
				spin_unlock(&region->status_lock);

				break;
			}
		case OMX_CMD_XEN_DESTROY_USER_REGION:
			{
				struct omx_endpoint *endpoint;
				struct omx_user_region *region;
				uint32_t eid, id;
				uint32_t request_id;
				uint8_t status;
				dprintk_deb
				    ("received backend request: OMX_CMD_XEN_DESTROY_USER_REGION, param=%lx\n",
				     sizeof(struct
					    omx_ring_msg_destroy_user_region));
				eid = resp->eid;
				id = resp->data.dur.id;
				status = resp->data.dur.status;
				request_id = resp->request_id;
				endpoint = omx_xenfront_get_endpoint(fe, resp);
				if (!endpoint) {
					printk_err("endpoint is NULL!!\n");
					break;
				}
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
				spin_lock(&region->status_lock);
				if (region) {
					if (!status) {
						//region->granted = 0;
						fe->requests[request_id] =
						    OMX_USER_REGION_STATUS_DEREGISTERED;
#ifdef OMX_XEN_FE_SHORTCUT
						endpoint->special_status_dereg =
						    OMX_USER_REGION_STATUS_DEREGISTERED;
#endif
					}
					else {
						printk_err
						    ("Failed to de-register user region%d\n",
						     id);
						//region->status =
						fe->requests[request_id] =
						    OMX_USER_REGION_STATUS_FAILED;
					}
				} else {
					printk_err("region pointer invalid!\n");
				}

				spin_unlock(&region->status_lock);
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
	uint32_t request_id;

	dprintk_in();

        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_XEN_GET_BOARD_COUNT;
	omx_poke_dom0(fe, ring_req);

	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_FREE); */
	if (ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_XEN_FRONTEND_STATUS_DOING, NULL) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);

	if (fe->requests[request_id] == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}

	*count = fe->board_count;

out:
	dprintk_out();
	return ret;
}

int omx_xen_peer_table_get_state(struct omx_cmd_peer_table_state *state)
{
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	int ret = 0;
	uint32_t request_id;

	dprintk_in();

        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_XEN_PEER_TABLE_GET_STATE;
	ring_req->board_index = 0;
	omx_poke_dom0(fe, ring_req);

	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_XEN_FRONTEND_STATUS_DOING, NULL)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);

	if (fe->requests[request_id] == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}
	memcpy(state, &fe->state, sizeof(*state));

out:
	dprintk_out();
	return ret;
}

int omx_xen_peer_table_set_state(struct omx_cmd_peer_table_state *state)
{
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	int ret = 0;
	uint32_t request_id;

	dprintk_in();

        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_XEN_PEER_TABLE_SET_STATE;
	ring_req->board_index = 0;
	memcpy(&ring_req->data.pts.state, &fe->state, sizeof(*state));
	omx_poke_dom0(fe, ring_req);

	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_XEN_FRONTEND_STATUS_DOING, NULL)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);

	if (fe->requests[request_id] == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}
        ret = 0;

out:
	dprintk_out();
	return ret;
}

int omx_xen_set_hostname(uint32_t board_index, const char *hostname)
{
	struct omx_xenfront_info *fe = __omx_xen_frontend;
	struct omx_xenif_request *ring_req;
	int ret = 0;
	uint32_t request_id;

	dprintk_in();

        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_XEN_SET_HOSTNAME;
	ring_req->board_index = board_index;
	memcpy(ring_req->data.sh.hostname, hostname, OMX_HOSTNAMELEN_MAX);

	omx_poke_dom0(fe, ring_req);

	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_XEN_FRONTEND_STATUS_DOING, NULL)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);

	if (fe->requests[request_id] == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}
        ret = 0;

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
	uint32_t request_id;

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

        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_GET_BOARD_INFO;
	ring_req->board_index = endpoint->board_index;
	ring_req->eid = endpoint->endpoint_index;
	dump_xen_get_board_info(&ring_req->data.gbi);
	omx_poke_dom0(endpoint->fe, ring_req);
	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_FREE); */
	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_XEN_FRONTEND_STATUS_DOING, NULL)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);

	memcpy(&get_board_info.info, &fe->board_info,
	       sizeof(struct omx_board_info));

	if (fe->requests[request_id] == OMX_XEN_FRONTEND_STATUS_FAILED) {
		ret = -EINVAL;
		goto out;
	}
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
	int ret = 0;
	uint32_t request_id;

	dprintk_in();
	dprintk_deb("bidx = %#lx, idx = %#lx\n", (unsigned long)board_index,
		    (unsigned long)endpoint_index);
	endpoint = fe->endpoints[endpoint_index];
	BUG_ON(!endpoint);

	spin_lock(&endpoint->status_lock);
	endpoint->info_status = OMX_ENDPOINT_STATUS_DOING;
	spin_unlock(&endpoint->status_lock);
        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
	ring_req->func = OMX_CMD_GET_ENDPOINT_INFO;
	ring_req->board_index = endpoint->board_index;
	ring_req->eid = endpoint->endpoint_index;
	dump_xen_get_endpoint_info(&ring_req->data.gei);
	omx_poke_dom0(endpoint->fe, ring_req);
	/* dprintk_deb("waiting to become %u\n", OMX_ENDPOINT_STATUS_DONE); */
	if ((ret = wait_for_backend_response
	    (&endpoint->info_status, OMX_ENDPOINT_STATUS_DOING,
	     &endpoint->status_lock)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);

        ret = 0;
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
	uint32_t request_id;

	dprintk_in();
	BUG_ON(!fe);
        ring_req = omx_ring_get_request(fe);
        request_id = (fe->ring.req_prod_pvt - 1) % OMX_MAX_INFLIGHT_REQUESTS;
        ring_req->request_id = request_id;
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
	omx_poke_dom0(fe, ring_req);
	if ((ret = wait_for_backend_response
	    (&fe->requests[request_id], OMX_XEN_FRONTEND_STATUS_DOING, NULL)) < 0) {
		printk_err("Failed to wait\n");
		ret = -EINVAL;
		goto out;
	}
        dprintk_deb("ret = %d\n", ret);
        ret = 0;

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
	if (fe->requests[request_id] == OMX_XEN_FRONTEND_STATUS_FAILED)
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
