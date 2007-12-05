/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
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

#ifndef __omx_wire_h__
#define __omx_wire_h__

/******************************
 * Packet definition
 */

#define DEFAULT_ETH_P_OMX 0x86DF

#ifndef ETH_P_OMX
#define ETH_P_OMX DEFAULT_ETH_P_OMX
#endif

enum omx_pkt_type {
	/* must start with NONE and end with MAX */
	OMX_PKT_TYPE_NONE=0,
	OMX_PKT_TYPE_RAW, /* FIXME: todo */
	OMX_PKT_TYPE_MFM_NIC_REPLY, /* FIXME: todo */
	OMX_PKT_TYPE_HOST_QUERY, /* FIXME: todo */
	OMX_PKT_TYPE_HOST_REPLY, /* FIXME: todo */

	OMX_PKT_TYPE_ETHER_UNICAST = 32, /* FIXME: todo */
	OMX_PKT_TYPE_ETHER_MULTICAST, /* FIXME: todo */
	OMX_PKT_TYPE_ETHER_NATIVE, /* FIXME: todo */
	OMX_PKT_TYPE_TRUC,
	OMX_PKT_TYPE_CONNECT,
	OMX_PKT_TYPE_TINY,
	OMX_PKT_TYPE_SMALL,
	OMX_PKT_TYPE_MEDIUM,
	OMX_PKT_TYPE_RNDV,
	OMX_PKT_TYPE_PULL,
	OMX_PKT_TYPE_PULL_REPLY,
	OMX_PKT_TYPE_NOTIFY,
	OMX_PKT_TYPE_NACK_LIB,
	OMX_PKT_TYPE_NACK_MCP,

	OMX_PKT_TYPE_MAX=255,
};

static inline const char*
omx_strpkttype(enum omx_pkt_type ptype)
{
	switch (ptype) {
	case OMX_PKT_TYPE_NONE:
		return "None";
	case OMX_PKT_TYPE_RAW:
		return "Raw";
	case OMX_PKT_TYPE_MFM_NIC_REPLY:
		return "MFM Nic Reply";
	case OMX_PKT_TYPE_HOST_QUERY:
		return "Host Query";
	case OMX_PKT_TYPE_HOST_REPLY:
		return "Host Reply";
	case OMX_PKT_TYPE_ETHER_UNICAST:
		return "Ether Unicast";
	case OMX_PKT_TYPE_ETHER_MULTICAST:
		return "Ether Multicast";
	case OMX_PKT_TYPE_ETHER_NATIVE:
		return "Ether Native";
	case OMX_PKT_TYPE_TRUC:
		return "Truc";
	case OMX_PKT_TYPE_CONNECT:
		return "Connect";
	case OMX_PKT_TYPE_TINY:
		return "Tiny";
	case OMX_PKT_TYPE_SMALL:
		return "Small";
	case OMX_PKT_TYPE_MEDIUM:
		return "Medium";
	case OMX_PKT_TYPE_RNDV:
		return "Rendez Vous";
	case OMX_PKT_TYPE_PULL:
		return "Pull";
	case OMX_PKT_TYPE_PULL_REPLY:
		return "Pull Reply";
	case OMX_PKT_TYPE_NOTIFY:
		return "Notify";
	case OMX_PKT_TYPE_NACK_LIB:
		return "Nack Lib";
	case OMX_PKT_TYPE_NACK_MCP:
		return "Nack MCP";
	default:
		return "** Unknown **";
	}
}

enum omx_nack_type {
	OMX_NACK_TYPE_NONE = 0,
	OMX_NACK_TYPE_BAD_ENDPT,
	OMX_NACK_TYPE_ENDPT_CLOSED,
	OMX_NACK_TYPE_BAD_SESSION,
	OMX_NACK_TYPE_BAD_RDMAWIN,
};

static inline const char*
omx_strnacktype(enum omx_nack_type ntype)
{
	switch (ntype) {
	case OMX_NACK_TYPE_NONE:
		return "None";
	case OMX_NACK_TYPE_BAD_ENDPT:
		return "Bad Endpoint";
	case OMX_NACK_TYPE_ENDPT_CLOSED:
		return "Endpoint Closed";
	case OMX_NACK_TYPE_BAD_SESSION:
		return "Bad Session";
	case OMX_NACK_TYPE_BAD_RDMAWIN:
		return "Bad RDMA Window";
	default:
		return "** Unknown **";
	}
}

#include <linux/if_ether.h>

struct omx_pkt_head {
	struct ethhdr eth;
	uint16_t dst_src_peer_index; /* MX's sender_peer_index */
	/* 16 */
};

struct omx_pkt_truc {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t length;
	uint8_t pad[3];
	uint32_t session;
	/* 12 */
};

struct omx_pkt_connect {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t length;
	uint8_t pad[3];
	uint16_t lib_seqnum;
	uint16_t src_dst_peer_index; /* MX's dest_peer_index */
	uint32_t pad0;
	/* 16 */
};

struct omx_pkt_msg {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint16_t length;
	uint16_t pad;
	uint16_t lib_seqnum;
	uint16_t lib_piggyack;
	uint32_t match_a;
	uint32_t match_b;
	uint32_t session;
	/* 24 */
};

struct omx_pkt_medium_frag { /* similar to MX's pkt_msg_t + pkt_frame_t */
	struct omx_pkt_msg msg;
	uint16_t frag_length;
	uint8_t frag_seqnum;
	uint8_t frag_pipeline;
	uint32_t pad;
	/* 24+8 */
};

struct omx_pkt_pull_request {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session;
	uint32_t total_length; /* total pull length */
#ifdef OMX_MX_WIRE_COMPAT
	uint8_t pulled_rdma_id;
	uint8_t pulled_rdma_seqnum; /* FIXME: unused ? */
	uint16_t pulled_rdma_offset;
#else
	uint32_t pulled_rdma_id;
	uint8_t pulled_rdma_seqnum; /* FIXME: unused ? */
	uint8_t pad[3];
	uint32_t pulled_rdma_offset; /* FIXME: we could use 64bits ? */
#endif
	uint32_t src_pull_handle; /* sender's handle id, MX's src_send_handle */
	uint32_t src_magic; /* sender's endpoint magic, MX's magic */
#ifdef OMX_MX_WIRE_COMPAT
	uint16_t first_frame_offset; /* pull iteration offset in the first frame (for the first iteration, set to pulled_rdma_offset), MX's offset */
	uint16_t block_length; /* current pull block length (nr * pagesize - target offset), MX's pull_length */
#else
	uint32_t first_frame_offset;
	uint32_t block_length;
#endif
	uint32_t frame_index; /* pull iteration index (page_nr/page_per_pull), MX's index */
	/* 32 in MX wire compat mode */
};

struct omx_pkt_pull_reply {
	uint8_t ptype;
	uint8_t frame_seqnum; /* sender's pull index + page number in this frame, %256 */
	uint16_t frame_length; /* pagesize - frame_offset */
	uint32_t msg_offset; /* index * pagesize - target_offset + sender_offset */
	uint32_t dst_pull_handle; /* sender's handle id */
	uint32_t dst_magic; /* sender's endpoint magic */
	/* 16 */
};

struct omx_pkt_notify {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session;
	uint32_t total_length;
	uint8_t puller_rdma_id;
	uint8_t puller_rdma_seqnum;
	uint16_t pad0[2];
	uint16_t lib_seqnum;
	uint16_t lib_piggyack;
	uint16_t pad1;
	/* 24 */
};

struct omx_pkt_nack_lib {
	uint8_t ptype;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	enum omx_nack_type nack_type;
	uint32_t pad;
	uint8_t pad0;
	uint8_t dst_endpoint;
	uint16_t dst_src_peer_index; /* MX's dest_peer_index */
	uint16_t lib_seqnum;
	uint16_t pad1;
	/* 16 */
};

struct omx_pkt_nack_mcp {
	uint8_t ptype;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	enum omx_nack_type nack_type;
	uint32_t pad;
	uint32_t src_pull_handle;
	uint32_t src_magic;
	/* 16 */
};

struct omx_hdr {
	struct omx_pkt_head head;
	/* 32 */
	union {
		struct omx_pkt_msg generic;
		struct omx_pkt_msg tiny;
		struct omx_pkt_msg small;
		struct omx_pkt_medium_frag medium;
		struct omx_pkt_msg rndv;
		struct omx_pkt_pull_request pull;
		struct omx_pkt_pull_reply pull_reply;
		struct omx_pkt_notify notify;
		struct omx_pkt_connect connect;
		struct omx_pkt_nack_lib nack_lib;
		struct omx_pkt_nack_mcp nack_mcp;
		struct omx_pkt_truc truc;
	} body;
};

/*
 * Pull Protocol
 *
 * The application passes a request containing
 * + req->length (total length to pull)
 * + req->remote_rdmawin_id/seqnum/offset (remote rdma id/seqnum/offset to pull from)
 * + req->local_rdmawin_id/seqnum/offset (local rdma id/seqnum/offset to push to)
 * The MCP creates a handle (id for this pull) containing info about the local rdmawin
 *
 * The MCP sends a pull with
 * + pull->total_length = the total_length of the pull
 * + pull->pulled_rdmawin_id/seqnum/offset = req->remote_rdmawin_id/seqnum/offset
 * + pull->src_pull_handle = internal handle id
 * + pull->magic = internal endpoint pull magic number
 * + pull->block_length = (PAGE*MAX_FRAMES_PER_PULL) - req->remote_rdma_offset,
 *        (align the transfer on page boundaries on the receiver's side)
 * + pull->pull_offset = req->target_rdma_offset
 * + pull->frame_index = 0
 *
 * Once this pull is done, a new one is sent with the following changes
 * + pull->block_length = PAGE*MAX_FRAMES_PER_PULL
 * + pull->pull_offset = 0
 * + pull->frame_index += MAX_FRAMES_PER_PULL
 *
 * When a pull arrives, the replier sends a pull_reply with
 * reply->frame_seqnum = pull->index
 * reply->frame_length = PAGE - pull->pulled_offset
 * reply->msg_offset = pull->index * PAGE - pull->pulled_rdma_offset + pull->pull_offset
 * reply->src_send_handle = pull->src_send_handle
 * reply->magic = pull->magic
 *
 * The next pull replies have the following changes
 * reply->frame_seqnum += 1
 * reply->frame_length += PAGE
 * reply->msg_offset += prev_frame_length
 *
 * The replier pulls reply->frame_length bytes from its rdmawin at offset
 * pull->offset first, then reply->frame_seqnum * PAGE
 * The puller writes reply->frame_length bytes to its rdmawin at offset
 * req->local_rdma_offset + reply->msg_offset
 */

#endif /* __omx_wire_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
