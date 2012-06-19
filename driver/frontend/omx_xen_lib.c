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

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_io.h"
#include "omx_wire.h"
#include "omx_xen.h"
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"

#if 0
struct omx_cmd_xen_send_notify {
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index;
	uint32_t eid;
	int ret;
	/* 8 */
	struct omx_cmd_send_notify notify;
#endif
};
struct omx_cmd_xen_send_liback {
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index;
	uint32_t eid;
	int ret;
	/* 8 */
	struct omx_cmd_send_liback liback;
	struct omx_cmd_send_liback {
#ifdef EXTRA_DEBUG_OMX
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t shared;
		uint32_t session_id;
		/* 8 */
		uint32_t acknum;
		uint16_t lib_seqnum;
		uint16_t send_seq;
		/* 16 */
		uint8_t resent;
		uint8_t pad[7];
		/* 24 */
#endif
	};

#endif
};
struct omx_cmd_xen_get_counters {
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index;
	uint8_t clear;
	uint8_t pad1[3];
	/* 8 */
	uint64_t buffer_addr;
	/* 16 */
	uint32_t buffer_length;
	int ret;
	/* 24 */
#endif
};

#endif

void dump_xen_recv_tiny(struct omx_cmd_xen_recv_msg *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret, i;
	struct omx_evt_recv_msg *msg = &info->msg;
	uint16_t peer_index = msg->peer_index;
	uint8_t src_endpoint = msg->src_endpoint;
	uint16_t seqnum = msg->seqnum;
	uint16_t piggyack = msg->piggyack;
	uint64_t match_info = msg->match_info;
	uint8_t length = msg->specific.tiny.length;
	uint8_t checksum = msg->specific.tiny.checksum;
	char *data = msg->specific.tiny.data;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%u,\n", peer_index,
		    src_endpoint);
	dprintk_deb("seqnum=%#x, piggyack=%#x, checksum=%#x\n", seqnum,
		    piggyack, checksum);
	dprintk_deb("match_info=%#llx, length=%#x\n", match_info, length);
	for (i = 0; i < length; i++)
		dprintk_deb("%c", data[i]);
	dprintk_out();
#endif
}

void dump_xen_send_tiny(struct omx_cmd_xen_send_tiny *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	int i = 0;
	struct omx_cmd_send_tiny_hdr *hdr = &info->tiny.hdr;
	char *data = info->tiny.data;
	uint16_t peer_index = hdr->peer_index;
	uint8_t dest_endpoint = hdr->dest_endpoint;
	uint8_t length = hdr->length;
	uint8_t shared = hdr->shared;
	uint32_t session_id = hdr->session_id;
	uint16_t seqnum = hdr->seqnum;
	uint16_t piggyack = hdr->piggyack;
	uint64_t match_info = hdr->match_info;
	uint16_t checksum = hdr->checksum;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, dest_endpoint=%u,\n", peer_index,
		    dest_endpoint);
	dprintk_deb("seqnum=%#x, piggyack=%#x\n", seqnum, piggyack);
	dprintk_deb("length=%#x, match_info=%#llx\n", length, match_info);
	dprintk_deb("session_id=%#x, checksum=%#x\n", session_id, checksum);
	dprintk_deb("shared=%#x\n", shared);
	for (i = 0; i < length; i++)
		dprintk_deb("%c", data[i]);
	dprintk_out();
#endif
}

void dump_xen_send_notify(struct omx_cmd_xen_send_notify *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_cmd_send_notify *notify = &info->notify;
	uint16_t peer_index = notify->peer_index;
	uint8_t dest_endpoint = notify->dest_endpoint;
	uint16_t seqnum = notify->seqnum;
	uint16_t piggyack = notify->piggyack;
	uint32_t session_id = notify->session_id;
	uint32_t total_length = notify->total_length;
	uint8_t pulled_rdma_id = notify->pulled_rdma_id;
	uint8_t pulled_rdma_seqnum = notify->pulled_rdma_seqnum;
	uint8_t shared = notify->shared;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, dest_endpoint=%u,\n", peer_index,
		    dest_endpoint);
	dprintk_deb("seqnum=%#x, piggyack=%#x\n", seqnum, piggyack);
	dprintk_deb("total_length=%#x\n", total_length);
	dprintk_deb("pulled_rdma_id=%#x, pulled_rmda_seqnum=%#x\n",
		    pulled_rdma_id, pulled_rdma_seqnum);
	dprintk_deb("session_id=%#x\n", session_id);
	dprintk_deb("shared=%#x\n", shared);
	dprintk_out();
#endif
}

void dump_xen_recv_liback(struct omx_cmd_xen_recv_liback *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_recv_liback *liback = &info->liback;
	uint16_t peer_index = liback->peer_index;
	uint8_t src_endpoint = liback->src_endpoint;
	uint32_t acknum = liback->acknum;
	uint16_t lib_seqnum = liback->lib_seqnum;
	uint16_t send_seq = liback->send_seq;
	uint8_t resent = liback->resent;
	uint8_t type = liback->type;
	uint8_t id = liback->id;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%#x,\n", peer_index,
		    src_endpoint);
	dprintk_deb("send_seq=%#x, resent=%#x\n", send_seq, resent);
	dprintk_deb("acknum=%#x, lib_seqnum=%#x\n", acknum, lib_seqnum);
	dprintk_deb("type=%#x, id=%#x\n", type, id);
	dprintk_out();

#endif
}

void dump_xen_send_liback(struct omx_cmd_xen_send_liback *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_cmd_send_liback *liback = &info->liback;
	uint16_t peer_index = liback->peer_index;
	uint8_t dest_endpoint = liback->dest_endpoint;
	uint8_t shared = liback->shared;
	uint32_t session_id = liback->session_id;
	uint32_t acknum = liback->acknum;
	uint16_t lib_seqnum = liback->lib_seqnum;
	uint16_t send_seq = liback->send_seq;
	uint8_t resent = liback->resent;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, dest_endpoint=%#x,\n", peer_index,
		    dest_endpoint);
	dprintk_deb("session_id=%#x, send_seq=%#x, resent=%#x\n", session_id,
		    send_seq, resent);
	dprintk_deb("shared =%#x, acknum=%#x, lib_seqnum=%#x\n", shared, acknum,
		    lib_seqnum);
	dprintk_out();

#endif
}

void dump_xen_recv_pull_done(struct omx_cmd_xen_recv_pull_done *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_pull_done *msg = &info->pull_done;
	uint64_t lib_cookie = msg->lib_cookie;
	uint32_t puller_rdma_id = msg->puller_rdma_id;
	uint8_t status = msg->status;
	uint8_t type = msg->type;
	uint8_t id = msg->id;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("puller_rdma_id=%#x, lib_cookie=%#llx\n", puller_rdma_id,
		    lib_cookie);
	dprintk_deb("status=%#x, type=%#x, id=%#x\n", status, type, id);
	dprintk_out();
#endif
}

void dump_xen_recv_pull_request(struct omx_cmd_xen_recv_pull_request *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_recv_pull_request *msg = &info->pull_req;
	uint8_t src_endpoint = msg->src_endpoint;
	uint8_t dst_endpoint = msg->dst_endpoint;
	uint16_t session_id = msg->session_id;
	uint32_t block_length = msg->block_length;
	uint32_t first_frame_offset = msg->first_frame_offset;
	uint32_t pulled_rdma_id = msg->pulled_rdma_id;
	uint32_t pulled_rdma_offset = msg->pulled_rdma_offset;
	uint32_t src_pull_handle = msg->src_pull_handle;
	uint32_t src_magic = msg->src_magic;
	uint32_t frame_index = msg->frame_index;
	uint16_t peer_index = msg->peer_index;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%#x, dst_endpoint=%#x,\n",
		    peer_index, src_endpoint, dst_endpoint);
	dprintk_deb
	    ("block_length=%#x, session_id=%#x, first_frame_offset=%#x\n",
	     block_length, session_id, first_frame_offset);
	dprintk_deb("pulled_rdma_id=%#x, pulled_rmda_offset=%#x\n",
		    pulled_rdma_id, pulled_rdma_offset);
	dprintk_deb("src_pull_handle =%#x, src_magic=%#x, frame_index=%#x\n",
		    src_pull_handle, src_magic, frame_index);
	dprintk_out();
#endif
}

void dump_xen_pull(struct omx_cmd_xen_pull *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_cmd_pull *msg = &info->pull;
	uint64_t lib_cookie = msg->lib_cookie;
	uint32_t puller_rdma_id = msg->puller_rdma_id;
	uint32_t pulled_rdma_offset = msg->pulled_rdma_offset;
	uint32_t pulled_rdma_id = msg->pulled_rdma_id;
	uint32_t pulled_rdma_seqnum = msg->pulled_rdma_seqnum;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("puller_rdma_id=%#x, lib_cookie=%#llx\n", puller_rdma_id,
		    lib_cookie);
	dprintk_deb("pulled_rdma_offset=%#x, pulled_rdma_seqnum=%#x\n",
		    pulled_rdma_offset, pulled_rdma_seqnum);
	dprintk_out();
#endif
}

void dump_xen_send_rndv(struct omx_cmd_xen_send_rndv *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_cmd_send_rndv *rndv = &info->rndv;
	uint16_t peer_index = rndv->peer_index;
	uint8_t dest_endpoint = rndv->dest_endpoint;
	uint16_t seqnum = rndv->seqnum;
	uint16_t piggyack = rndv->piggyack;
	uint64_t match_info = rndv->match_info;
	uint32_t msg_length = rndv->msg_length;
	uint8_t pulled_rdma_id = rndv->pulled_rdma_id;
	uint8_t pulled_rdma_seqnum = rndv->pulled_rdma_seqnum;
	uint8_t checksum = rndv->checksum;
	uint8_t shared = rndv->shared;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, dest_endpoint=%u,\n", peer_index,
		    dest_endpoint);
	dprintk_deb("seqnum=%#x, piggyack=%#x, checksum=%#x\n", seqnum,
		    piggyack, checksum);
	dprintk_deb("match_info=%#llx, msg_length=%#x\n", match_info,
		    msg_length);
	dprintk_deb("pulled_rdma_id=%#x, pulled_rmda_seqnum=%#x\n",
		    pulled_rdma_id, pulled_rdma_seqnum);
	dprintk_deb("shared=%#x\n", shared);
	dprintk_out();
#endif
}

void dump_xen_recv_notify(struct omx_cmd_xen_recv_msg *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_recv_msg *msg = &info->msg;
	uint16_t peer_index = msg->peer_index;
	uint8_t src_endpoint = msg->src_endpoint;
	uint16_t seqnum = msg->seqnum;
	uint16_t piggyack = msg->piggyack;
	uint32_t length = msg->specific.notify.length;
	uint8_t pulled_rdma_id = msg->specific.notify.pulled_rdma_id;
	uint8_t pulled_rdma_seqnum = msg->specific.notify.pulled_rdma_seqnum;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%u,\n", peer_index,
		    src_endpoint);
	dprintk_deb("seqnum=%#x, piggyack=%#x, \n", seqnum, piggyack);
	dprintk_deb("length=%#x\n", length);
	dprintk_deb("pulled_rdma_id=%#x, pulled_rmda_seqnum=%#x, \n",
		    pulled_rdma_id, pulled_rdma_seqnum);
	dprintk_out();
#endif
}

void dump_xen_recv_msg(struct omx_cmd_xen_recv_msg *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_recv_msg *msg = &info->msg;
	uint16_t peer_index = msg->peer_index;
	uint8_t src_endpoint = msg->src_endpoint;
	uint16_t seqnum = msg->seqnum;
	uint16_t piggyack = msg->piggyack;
	uint64_t match_info = msg->match_info;
	uint32_t msg_length = msg->specific.rndv.msg_length;
	uint8_t pulled_rdma_id = msg->specific.rndv.pulled_rdma_id;
	uint8_t pulled_rdma_seqnum = msg->specific.rndv.pulled_rdma_seqnum;
	uint16_t pulled_rdma_offset = msg->specific.rndv.pulled_rdma_offset;
	uint8_t checksum = msg->specific.rndv.checksum;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%u,\n", peer_index,
		    src_endpoint);
	dprintk_deb("seqnum=%#x, piggyack=%#x, checksum=%#x\n", seqnum,
		    piggyack, checksum);
	dprintk_deb("match_info=%#llx, msg_length=%#x\n", match_info,
		    msg_length);
	dprintk_deb
	    ("pulled_rdma_id=%#x, pulled_rmda_seqnum=%#x, pulled_rmda_offset=%#x\n",
	     pulled_rdma_id, pulled_rdma_seqnum, pulled_rdma_offset);
	dprintk_out();
#endif
}

void dump_xen_recv_connect_request(struct omx_cmd_xen_recv_connect_request
				   *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_recv_connect_request *req = &info->request;
	uint16_t peer_index = req->peer_index;
	uint8_t src_endpoint = req->src_endpoint;
	uint16_t seqnum = req->seqnum;
	uint32_t src_session_id = req->src_session_id;
	uint32_t app_key = req->app_key;
	uint16_t target_recv_seqnum_start = req->target_recv_seqnum_start;
	uint8_t connect_seqnum = req->connect_seqnum;
	uint8_t shared = req->shared;
	uint8_t type = req->type;
	uint8_t id = req->id;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%u, shared_disabled=%u\n",
		    peer_index, src_endpoint, shared);
	dprintk_deb("seqnum=%#x, src_session_id=%#x, app_key=%u\n", seqnum,
		    src_session_id, app_key);
	dprintk_deb("target_recv_seqnum_start=%#x, connect_seqnum=%#x\n",
		    target_recv_seqnum_start, connect_seqnum);
	dprintk_deb("shared=%#x, type=%#x, id=%#x\n", shared, type, id);
	dprintk_out();
#endif
}

void dump_xen_recv_connect_reply(struct omx_cmd_xen_recv_connect_reply *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_evt_recv_connect_reply *reply = &info->reply;
	uint16_t peer_index = reply->peer_index;
	uint8_t src_endpoint = reply->src_endpoint;
	uint16_t seqnum = reply->seqnum;
	uint32_t src_session_id = reply->src_session_id;
	uint32_t target_session_id = reply->target_session_id;
	uint16_t target_recv_seqnum_start = reply->target_recv_seqnum_start;
	uint8_t connect_seqnum = reply->connect_seqnum;
	uint8_t connect_status_code = reply->connect_status_code;
	uint8_t shared = reply->shared;
	uint8_t type = reply->type;
	uint8_t id = reply->id;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, src_endpoint=%u, shared=%u\n", peer_index,
		    src_endpoint, shared);
	dprintk_deb("seqnum=%#x, src_session_id=%#x, target_session_id=%#x\n",
		    seqnum, src_session_id, target_session_id);
	dprintk_deb("target_recv_seqnum_start=%#x, connect_seqnum=%#x\n",
		    target_recv_seqnum_start, connect_seqnum);
	dprintk_deb("connect_status_code=%#x\n", connect_status_code);
	dprintk_deb("shared=%#x, type=%#x, id=%#x\n", shared, type, id);
	dprintk_out();
#endif
}

void dump_xen_send_connect_request(struct omx_cmd_xen_send_connect_request
				   *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_cmd_send_connect_request *req = &info->request;
	uint16_t peer_index = req->peer_index;
	uint8_t dest_endpoint = req->dest_endpoint;
	uint8_t shared_disabled = req->shared_disabled;
	uint16_t seqnum = req->seqnum;
	uint32_t src_session_id = req->src_session_id;
	uint32_t app_key = req->app_key;
	uint16_t target_recv_seqnum_start = req->target_recv_seqnum_start;
	uint8_t connect_seqnum = req->connect_seqnum;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, dest_endpoint=%u, shared_disabled=%u\n",
		    peer_index, dest_endpoint, shared_disabled);
	dprintk_deb("seqnum=%#x, src_session_id=%#x, app_key=%u\n", seqnum,
		    src_session_id, app_key);
	dprintk_deb("target_recv_seqnum_start=%#x, connect_seqnum=%#x\n",
		    target_recv_seqnum_start, connect_seqnum);
	dprintk_out();
#endif
}

void dump_xen_send_connect_reply(struct omx_cmd_xen_send_connect_reply *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_cmd_send_connect_reply *reply = &info->reply;
	uint16_t peer_index = reply->peer_index;
	uint8_t dest_endpoint = reply->dest_endpoint;
	uint8_t shared_disabled = reply->shared_disabled;
	uint16_t seqnum = reply->seqnum;
	uint32_t src_session_id = reply->src_session_id;
	uint16_t target_recv_seqnum_start = reply->target_recv_seqnum_start;
	uint8_t connect_seqnum = reply->connect_seqnum;
	uint32_t target_session_id = reply->target_session_id;
	uint8_t connect_status_code = reply->connect_status_code;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("peer_index=%#x, dest_endpoint=%u, shared_disabled=%u\n",
		    peer_index, dest_endpoint, shared_disabled);
	dprintk_deb("seqnum=%#x, src_session_id=%#x, target_session_id=%#x\n",
		    seqnum, src_session_id, target_session_id);
	dprintk_deb("target_recv_seqnum_start=%#x, connect_seqnum=%#x\n",
		    target_recv_seqnum_start, connect_seqnum);
	dprintk_deb("connect_status_code=%#x\n", connect_status_code);
	dprintk_out();
#endif
}

void dump_xen_get_board_info(struct omx_cmd_xen_get_board_info *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_board_info *binfo = &info->info;
	uint64_t addr = binfo->addr;
	uint32_t mtu = binfo->mtu;
	uint32_t numa_node = binfo->numa_node;
	uint32_t status = binfo->status;
	char *hostname = binfo->hostname;
	char *ifacename = binfo->ifacename;
	char *drivername = binfo->drivername;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("addr=%#llx, mtu=%u, numa_node=%u, status=%u\n", addr, mtu,
		    numa_node, status);
	dprintk_deb("hostname=%s, ifacename=%s, drivername=%s\n", hostname,
		    ifacename, drivername);
	dprintk_out();
#endif
}

void dump_xen_get_endpoint_info(struct omx_cmd_xen_get_endpoint_info *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	uint32_t eid = info->eid;
	int ret = info->ret;
	struct omx_endpoint_info *einfo = &info->info;
	uint32_t closed = einfo->closed;
	uint32_t pid = einfo->pid;
	char *command = einfo->command;

	dprintk_in();
	dprintk_deb("board_index=%#lx, eid=%#lx, ret=%d\n",
		    (unsigned long)board_index, (unsigned long)eid, ret);
	dprintk_deb("closed=%u, pid=%u\n", closed, pid);
	dprintk_deb("command=%s\n", command);
	dprintk_out();
#endif
}

void dump_xen_set_hostname(struct omx_cmd_xen_set_hostname *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t board_index = info->board_index;
	//uint32_t eid = info->eid;
	//int ret = info->ret;
	char *hostname = info->hostname;

	dprintk_in();
	dprintk_deb("board_index=%#lx, hostname=%s\n",
		    (unsigned long)board_index, hostname);
	dprintk_out();
#endif
}

void dump_xen_misc_peer_info(struct omx_cmd_xen_misc_peer_info *info)
{
#ifdef EXTRA_DEBUG_OMX
	int ret = info->ret;
	//uint32_t board_index = info->board_index;
	//uint32_t eid = info->eid;
	struct omx_cmd_misc_peer_info *pinfo = &info->info;
	uint64_t board_addr = pinfo->board_addr;
	char *hostname = pinfo->hostname;
	uint32_t index = pinfo->index;

	dprintk_in();
	dprintk_deb("board_addr=%#llx, hostname=%s\n", board_addr, hostname);
	dprintk_deb("index=%#x, ret=%d\n", index, ret);
	dprintk_out();
#endif
}

void dump_xen_bench(struct omx_cmd_xen_bench *info)
{
#ifdef EXTRA_DEBUG_OMX
	struct omx_cmd_bench_hdr *bhdr = &info->hdr;
	uint8_t type = bhdr->type;
	char *dummy_data = info->dummy_data;

	dprintk_in();
	dprintk_deb("type=%u \n", type);
	dprintk_deb("dummy_data=%s\n", dummy_data);
	dprintk_out();
#endif
}

#if 0
void dump_xen_peer_table_state(struct omx_cmd_xen_peer_table_state *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t status = info->status;
	uint32_t version = info->version;
	uint32_t size = info->size;
	uint64_t mapper_id = info->mapper_id;

	dprintk_in();
	dprintk_deb("status=%u, size=%u \n", status, size);
	dprintk_deb("version=%u, mapper_id=%#llx\n", version, mapper_id);
	dprintk_out();
#endif
}
#endif

void dump_xen_ring_msg_register_user_segment(struct
					     omx_ring_msg_register_user_segment
					     *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t rid = info->rid;
	uint32_t eid = info->eid;
	uint32_t aligned_vaddr = info->eid;
	uint16_t first_page_offset = info->first_page_offset;
	uint16_t status = info->status;
	uint32_t length = info->length;
	uint32_t nr_pages = info->nr_pages;
	uint32_t nr_grefs = info->nr_grefs;
	uint32_t sid = info->sid;

	dprintk_in();
	dprintk_deb("rid=%#x, eid=%#x, sid=%#x\n", rid, eid, sid);
	dprintk_deb("status=%u, length=%#x \n", status, length);
	dprintk_deb("aligned_vaddr=%#x, first_page_offset=%#x\n", aligned_vaddr,
		    first_page_offset);
	dprintk_deb("nr_pages=%#x, nr_grefs=%#x\n", nr_pages, nr_grefs);
	dprintk_out();
#endif
}

void dump_xen_ring_msg_deregister_user_segment(struct
					       omx_ring_msg_deregister_user_segment
					       *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t rid = info->rid;
	uint32_t eid = info->eid;
	uint32_t aligned_vaddr = info->eid;
	uint16_t first_page_offset = info->first_page_offset;
	uint16_t status = info->status;
	uint32_t length = info->length;
	uint32_t nr_pages = info->nr_pages;
	uint32_t nr_grefs = info->nr_grefs;
	uint32_t sid = info->sid;

	dprintk_in();
	dprintk_deb("rid=%#x, eid=%#x, sid=%#x\n", rid, eid, sid);
	dprintk_deb("status=%u, length=%#x \n", status, length);
	dprintk_deb("aligned_vaddr=%#x, first_page_offset=%#x\n", aligned_vaddr,
		    first_page_offset);
	dprintk_deb("nr_pages=%#x, nr_grefs=%#x\n", nr_pages, nr_grefs);
	dprintk_out();
#endif
}

void dump_xen_ring_msg_create_user_region(struct omx_ring_msg_create_user_region
					  *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t id = info->id;
	uint32_t nr_segments = info->nr_segments;
	uint32_t seqnum = info->seqnum;
	uint16_t offset = info->offset;
	uint8_t eid = info->eid;
	uint8_t status = info->status;
	uint64_t vaddr = info->vaddr;
	uint32_t nr_grefs = info->nr_grefs;
	uint32_t nr_pages = info->nr_pages;

	dprintk_in();
	dprintk_deb("id=%#x, eid=%#x, seqnum=%#x\n", id, eid, seqnum);
	dprintk_deb("status=%u, nr_segments=%#x\n", status, nr_segments);
	dprintk_deb("vaddr=%#llx, first_page_offset=%#x\n", vaddr, offset);
	dprintk_deb("nr_pages=%#x, nr_grefs=%#x\n", nr_pages, nr_grefs);
	dprintk_out();
#endif
}

void dump_xen_ring_msg_destroy_user_region(struct
					   omx_ring_msg_destroy_user_region
					   *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint32_t id = info->id;
	uint32_t seqnum = info->seqnum;
	uint8_t eid = info->eid;
	uint8_t status = info->status;
	uint64_t region = info->region;

	dprintk_in();
	dprintk_deb("id=%#x, eid=%#x, seqnum=%#x\n", id, eid, seqnum);
	dprintk_deb("status=%u\n", status);
	dprintk_deb("region=%#llx \n", region);
	dprintk_out();
#endif
}

void dump_xen_ring_msg_endpoint(struct omx_ring_msg_endpoint *info)
{
#ifdef EXTRA_DEBUG_OMX
	uint8_t board_index = info->board_index;
	uint8_t endpoint_index = info->endpoint_index;
	int16_t ret = info->ret;
	uint32_t session_id = info->session_id;
	struct omx_endpoint *endpoint = info->endpoint;

	dprintk_in();
	dprintk_deb("bid=%#x, eid=%#x\n", board_index, endpoint_index);
	dprintk_deb("ret=%d, session_id=%#x\n", ret, session_id);
	dprintk_deb("endpoint=%#lx\n", (unsigned long)endpoint);
	dprintk_out();
#endif
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
