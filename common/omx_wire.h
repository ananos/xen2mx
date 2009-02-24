/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

/***************
 * Misc helpers
 */

#ifndef max
#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })
#endif


/************
 * Constants
 */

/* EtherType */
#define DEFAULT_ETH_P_OMX 0x86DF
#ifndef ETH_P_OMX
#define ETH_P_OMX DEFAULT_ETH_P_OMX
#endif

#define OMX_PULL_REPLY_PAYLOAD_OF_MTU(x) (x-sizeof(struct omx_pkt_head)-sizeof(struct omx_pkt_pull_reply))
#define OMX_PULL_REPLY_MTU_OF_PAYLOAD(x) (x+sizeof(struct omx_pkt_head)+sizeof(struct omx_pkt_pull_reply))

#define OMX_MEDIUM_FRAG_PAYLOAD_OF_MTU(x) (x-sizeof(struct omx_pkt_head)-sizeof(struct omx_pkt_medium_frag))
#define OMX_MEDIUM_FRAG_MTU_OF_PAYLOAD(x) (x+sizeof(struct omx_pkt_head)+sizeof(struct omx_pkt_medium_frag))

#ifdef OMX_MX_WIRE_COMPAT

/*
 * MX uses 4096 payload max, plus headers.
 * that's not really a MTU, but we need it to simplify things.
 */
# ifdef OMX_MTU
#  error OMX_MTU should not be defined in wire-compatible mode
# endif
# define OMX_PULL_REPLY_LENGTH_MAX		4096
# define OMX_MEDIUM_FRAG_LENGTH_MAX		4096
# define OMX_MEDIUM_FRAG_LENGTH_SHIFT		12 /* the exact power-of-two for the max length, only needed in wire-compat mode */
# define OMX_MEDIUM_FRAG_LENGTH_ROUNDUPSHIFT	12 /* the power-of-two above or equal to the max length */
# define OMX_MTU ((unsigned) (sizeof(struct omx_pkt_head)						\
			      + max( sizeof(struct omx_pkt_medium_frag) + OMX_MEDIUM_FRAG_LENGTH_MAX,	\
				     sizeof(struct omx_pkt_pull_reply) + OMX_PULL_REPLY_LENGTH_MAX )	\
				    ))

#else /* !OMX_MX_WIRE_COMPAT */

/* configure-enforced MTU */
# ifndef OMX_MTU
#  error OMX_MTU should be defined in non-wire-compatible mode
# endif

/*
 * large message fragments use the full MTU all the time if non-wire compatible mode.
 */
#define OMX_PULL_REPLY_LENGTH_MAX OMX_PULL_REPLY_PAYLOAD_OF_MTU(OMX_MTU)

/*
 * As long as a packet is under 4kB, use the exact MTU-hdrlen for medium and large fragments.
 * After 4kB, we may need more than a page, so just round to the power-of-two below (4kB or 8kB)
 */
#define OMX_MEDIUM_FRAG_LENGTH_MAX (					\
  OMX_MEDIUM_FRAG_PAYLOAD_OF_MTU(OMX_MTU) <= 4096			\
    ? OMX_MEDIUM_FRAG_PAYLOAD_OF_MTU(OMX_MTU)				\
    : ( OMX_MEDIUM_FRAG_MTU_OF_PAYLOAD(8192) > OMX_MTU ? 4096 : 8192 )	\
)

/*
 * the power-of-two above or equal to the above OMX_MEDIUM_FRAG_LENGTH_MAX.
 * it is used to allocate sendq/recvq rings
 */
#define OMX_MEDIUM_FRAG_LENGTH_ROUNDUPSHIFT (			\
  OMX_PULL_REPLY_PAYLOAD_OF_MTU(OMX_MTU) <= 1024		\
    ? 10							\
    : OMX_PULL_REPLY_PAYLOAD_OF_MTU(OMX_MTU) <= 2048		\
      ? 11							\
      : OMX_PULL_REPLY_PAYLOAD_OF_MTU(OMX_MTU) <= 4096		\
	? 12							\
	: OMX_MEDIUM_FRAG_MTU_OF_PAYLOAD(8192) > OMX_MTU	\
	  ? 12							\
	  : 13							\
)

#endif /* !OMX_MX_WIRE_COMPAT */

#define OMX_ENDPOINT_INDEX_MAX 256
#define OMX_PEER_INDEX_MAX 65536

/******************
 * Packet Subtypes
 */

enum omx_pkt_type {
	/* must start with NONE and end with MAX */
	OMX_PKT_TYPE_NONE=0,
	OMX_PKT_TYPE_RAW,
	OMX_PKT_TYPE_MFM_NIC_REPLY, /* FIXME: todo */
	OMX_PKT_TYPE_HOST_QUERY,
	OMX_PKT_TYPE_HOST_REPLY,

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

typedef uint8_t omx_packet_type_t; /* don't use enum since it may end-up being stored on 32bits unless -fshort-enums is passed */

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

/***********************
 * Nack Packet Subtypes
 */

enum omx_nack_type {
	OMX_NACK_TYPE_NONE = 0,
	OMX_NACK_TYPE_BAD_ENDPT,
	OMX_NACK_TYPE_ENDPT_CLOSED,
	OMX_NACK_TYPE_BAD_SESSION,
	OMX_NACK_TYPE_BAD_RDMAWIN,
	OMX_NACK_TYPE_MAX,
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

/*********************
 * Packet definitions
 */

#include <linux/if_ether.h>

struct omx_pkt_head {
	struct ethhdr eth;
	uint16_t dst_src_peer_index; /* MX's sender_peer_index */
	/* 16 */
};

#define OMX_HDR_PTYPE_OFFSET sizeof(struct omx_pkt_head)

struct omx_pkt_host_query {
	omx_packet_type_t ptype;
	uint8_t pad;
	uint16_t src_dst_peer_index;
	uint32_t pad0;
	/* 8 */
	uint32_t magic;
	uint32_t pad1;
	/* 16 */
};

struct omx_pkt_host_reply {
	omx_packet_type_t ptype;
	uint8_t length;
	uint16_t src_dst_peer_index;
	uint32_t pad0;
	/* 8 */
	uint32_t magic;
	uint32_t pad1;
	/* 16 */
};

struct omx_pkt_truc {
	omx_packet_type_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t length;
	uint8_t pad1[3];
	/* 8 */
	uint32_t session;
	/* 12 */
};

struct omx_pkt_connect {
	omx_packet_type_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t length;
	uint8_t pad1[3];
	/* 8 */
	uint16_t lib_seqnum;
	uint16_t src_dst_peer_index; /* MX's dest_peer_index */
	uint32_t pad2;
	/* 16 */
};

struct omx_pkt_msg {
	omx_packet_type_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint16_t length;
	uint16_t pad1;
	/* 8 */
	uint16_t lib_seqnum;
	uint16_t lib_piggyack;
	uint32_t match_a;
	/* 16 */
	uint32_t match_b;
	uint32_t session;
	/* 24 */
};

struct omx_pkt_medium_frag { /* similar to MX's pkt_msg_t + pkt_frame_t */
	struct omx_pkt_msg msg;
	/* 24 */
	uint16_t frag_length;
	uint8_t frag_seqnum;
#ifdef OMX_MX_WIRE_COMPAT
	uint8_t frag_pipeline;
#else
	uint8_t pad1;
#endif
	uint32_t pad2;
	/* 32 */
};

#ifdef OMX_MX_WIRE_COMPAT
struct omx_pkt_pull_request {
	omx_packet_type_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session;
	/* 8 */
	uint32_t total_length; /* total pull length */
	uint8_t pulled_rdma_id;
	uint8_t pulled_rdma_seqnum; /* FIXME: unused ? */
	uint16_t pulled_rdma_offset;
	/* 16 */
	uint32_t src_pull_handle; /* sender's handle id, MX's src_send_handle */
	uint32_t src_magic; /* sender's endpoint magic, MX's magic */
	/* 24 */
	uint16_t first_frame_offset; /* pull iteration offset in the first frame (for the first iteration, set to pulled_rdma_offset), MX's offset */
	uint16_t block_length; /* current pull block length (nr * pagesize - target offset), MX's pull_length */
	uint32_t frame_index; /* pull iteration index (page_nr/page_per_pull), MX's index */
	/* 32 */
};
#else /* !OMX_MX_WIRE_COMPAT */
struct omx_pkt_pull_request {
	omx_packet_type_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session;
	/* 8 */
	uint32_t total_length; /* total pull length */
	uint32_t pulled_rdma_id;
	/* 16 */
	uint8_t pulled_rdma_seqnum; /* FIXME: unused ? */
	uint8_t pad1[3];
	uint32_t pulled_rdma_offset; /* FIXME: we could use 64bits ? */
	/* 24 */
	uint32_t src_pull_handle; /* sender's handle id, MX's src_send_handle */
	uint32_t src_magic; /* sender's endpoint magic, MX's magic */
	/* 32 */
	uint32_t first_frame_offset;
	uint32_t block_length;
	/* 40 */
	uint32_t frame_index; /* pull iteration index (page_nr/page_per_pull), MX's index */
	/* 44 */
};
#endif /* !OMX_MX_WIRE_COMPAT */

#ifdef OMX_MX_WIRE_COMPAT
#define OMX_PULL_REPLY_PER_BLOCK 8
#else
#define OMX_PULL_REPLY_PER_BLOCK 32
#endif

#define OMX_PULL_BLOCK_LENGTH_MAX (OMX_PULL_REPLY_LENGTH_MAX*OMX_PULL_REPLY_PER_BLOCK)

/* OMX_PULL_REPLY_LENGTH_MAX must fit inside pull_request.first_frame_offset */
/* OMX_PULL_BLOCK_LENGTH_MAX must fit inside pull_request.block_length */

struct omx_pkt_pull_reply {
	omx_packet_type_t ptype;
	uint8_t frame_seqnum; /* sender's pull index + page number in this frame, %256 */
	uint16_t frame_length; /* pagesize - frame_offset */
	uint32_t msg_offset; /* index * pagesize - target_offset + sender_offset */
	/* 8 */
	uint32_t dst_pull_handle; /* sender's handle id */
	uint32_t dst_magic; /* sender's endpoint magic */
	/* 16 */
};

struct omx_pkt_notify {
	omx_packet_type_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session;
	/* 8 */
	uint32_t total_length;
	uint8_t puller_rdma_id;
	uint8_t puller_rdma_seqnum;
	uint16_t pad1;
	/* 16 */
	uint16_t pad2;
	uint16_t lib_seqnum;
	uint16_t lib_piggyack;
	uint16_t pad3;
	/* 24 */
};

struct omx_pkt_nack_lib {
	omx_packet_type_t ptype;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t nack_type;
	uint32_t pad1;
	/* 8 */
	uint8_t pad2;
	uint8_t dst_endpoint;
	uint16_t dst_src_peer_index; /* MX's dest_peer_index */
	uint16_t lib_seqnum;
	uint16_t pad3;
	/* 16 */
};

struct omx_pkt_nack_mcp {
	omx_packet_type_t ptype;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint8_t nack_type;
	uint32_t pad1;
	/* 8 */
	uint32_t src_pull_handle;
	uint32_t src_magic;
	/* 16 */
};

struct omx_hdr {
	struct omx_pkt_head head;
	/* 16 */
	union {
		struct omx_pkt_host_query host_query;
		struct omx_pkt_host_reply host_reply;
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
 * + pull->src_magic = internal endpoint pull magic number
 * + pull->block_length = PULL_REPLY_LENGTH_MAX*MAX_FRAMES_PER_PULL - req->remote_rdma_offset,
 *        (align the transfer on page boundaries on the receiver's side)
 * + pull->first_frame_offset = req->remote_offset
 * + pull->frame_index = 0
 *
 * Once this pull is done, a new one is sent with the following changes
 * + pull->block_length = PULL_REPLY_LENGTH_MAX*MAX_FRAMES_PER_PULL
 * + pull->pull_offset = 0
 * + pull->frame_index += MAX_FRAMES_PER_PULL
 *
 * When a pull arrives, the replier sends a pull_reply with
 * reply->frame_seqnum = pull->frame_index
 * reply->frame_length = PULL_REPLY_LENGTH_MAX - pull->pulled_offset
 * reply->msg_offset = pull->frame_index*PULL_REPLY_LENGTH_MAX - pull->pulled_rdma_offset + pull->first_frame_offset
 * reply->src_send_handle = pull->src_send_handle
 * reply->magic = pull->magic
 *
 * The next pull replies have the following changes
 * reply->frame_seqnum += 1
 * reply->frame_length = PULL_REPLY_LENGTH_MAX
 * reply->msg_offset += previos frame_length
 *
 * The replier pulls reply->frame_length bytes from its rdmawin at offset
 *   frame_index * PULL_REPLY_LENGTH_MAX + pull->first_frame_offset
 * first, then at the same + the previous frame length, ...
 * The puller writes reply->frame_length bytes to its rdmawin at offset
 *   req->local_rdma_offset + reply->msg_offset
 * first, then...
 */

#endif /* __omx_wire_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
