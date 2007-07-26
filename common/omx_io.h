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

#ifndef __omx_io_h__
#define __omx_io_h__

#ifndef __KERNEL__
#include <stdint.h>
#endif

/************************
 * Common parameters or IOCTL subtypes
 */

#define OMX_SENDQ_ENTRY_SIZE	4096
#define OMX_SENDQ_ENTRY_NR	1024
#define OMX_SENDQ_SIZE		(OMX_SENDQ_ENTRY_SIZE*OMX_SENDQ_ENTRY_NR)
#define OMX_SENDQ_FILE_OFFSET	0

#define OMX_RECVQ_ENTRY_SIZE	4096
#define OMX_RECVQ_ENTRY_NR	1024
#define OMX_RECVQ_SIZE		(OMX_RECVQ_ENTRY_SIZE*OMX_RECVQ_ENTRY_NR)
#define OMX_RECVQ_FILE_OFFSET	4096

#define OMX_EVENTQ_ENTRY_SIZE	64
#define OMX_EVENTQ_ENTRY_NR	1024
#define OMX_EVENTQ_SIZE		(OMX_EVENTQ_ENTRY_SIZE*OMX_EVENTQ_ENTRY_NR)
#define OMX_EVENTQ_FILE_OFFSET	(2*4096)

#define OMX_TINY_MAX		32
#define OMX_SMALL_MAX		128 /* at most 4096? FIXME: check that it fits in a linear skb and a recvq page */

#define OMX_HOSTNAMELEN_MAX	80

#define OMX_USER_REGION_MAX	255
typedef uint8_t omx_user_region_id_t;

struct omx_cmd_region_segment {
	uint64_t vaddr;
	uint32_t len;
	uint32_t pad;
};

/************************
 * IOCTL commands
 */

#define OMX_CMD_GET_BOARD_MAX		0x01
#define OMX_CMD_GET_ENDPOINT_MAX	0x02
#define OMX_CMD_GET_PEER_MAX		0x03
#define OMX_CMD_GET_BOARD_COUNT		0x04
#define OMX_CMD_GET_BOARD_ID		0x05
#define OMX_CMD_OPEN_ENDPOINT		0x81
#define OMX_CMD_CLOSE_ENDPOINT		0x82
#define OMX_CMD_SEND_TINY		0x83
#define OMX_CMD_SEND_SMALL		0x84
#define OMX_CMD_SEND_MEDIUM		0x85
#define OMX_CMD_SEND_RENDEZ_VOUS	0x86
#define OMX_CMD_SEND_PULL		0x87
#define OMX_CMD_REGISTER_REGION		0x88
#define OMX_CMD_DEREGISTER_REGION	0x89

static inline const char *
omx_strcmd(unsigned cmd)
{
	switch (cmd) {
	case OMX_CMD_GET_BOARD_MAX:
		return "Get Board Max";
	case OMX_CMD_GET_ENDPOINT_MAX:
		return "Get Endpoint Max";
	case OMX_CMD_GET_PEER_MAX:
		return "Get Peer Max";
	case OMX_CMD_GET_BOARD_COUNT:
		return "Get Board Count";
	case OMX_CMD_GET_BOARD_ID:
		return "Get Board ID";
	case OMX_CMD_OPEN_ENDPOINT:
		return "Open Endpoint";
	case OMX_CMD_CLOSE_ENDPOINT:
		return "Close Endpoint";
	case OMX_CMD_SEND_TINY:
		return "Send Tiny";
	case OMX_CMD_SEND_SMALL:
		return "Send Small";
	case OMX_CMD_SEND_MEDIUM:
		return "Send Medium";
	case OMX_CMD_SEND_RENDEZ_VOUS:
		return "Send Rendez-vous";
	case OMX_CMD_SEND_PULL:
		return "Send Pull";
	case OMX_CMD_REGISTER_REGION:
		return "Register Region";
	case OMX_CMD_DEREGISTER_REGION:
		return "Deregister Region";
	default:
		return "** Unknown **";
	}
}

/************************
 * IOCTL parameter types
 */

struct omx_cmd_get_board_id {
	uint8_t board_index;
	uint64_t board_addr;
	char board_name[OMX_HOSTNAMELEN_MAX];
};

struct omx_cmd_open_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;
};

struct omx_cmd_send_tiny {
	struct omx_cmd_send_tiny_hdr {
		uint64_t dest_addr;
		uint8_t dest_endpoint;
		uint8_t length;
		/* 8 */
		uint64_t match_info;
		/* 16 */
		uint16_t seqnum;
		uint16_t pad;
		/* 20 */
	} hdr;
	char data[OMX_TINY_MAX];
	/* 52 */
};

struct omx_cmd_send_small {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad1;
	/* 8 */
	uint16_t length;
	uint16_t seqnum;
	uint32_t pad2;
	/* 16 */
	uint64_t vaddr;
	uint64_t match_info;
	/* 32 */
};

struct omx_cmd_send_medium {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad1;
	/* 8 */
	uint32_t msg_length;
	uint16_t frag_length;
	uint8_t frag_seqnum;
	uint8_t frag_pipeline;
	/* 16 */
	uint16_t seqnum;
	uint16_t sendq_page_offset;
	uint32_t pad2;
	/* 24 */
	uint64_t match_info;
	/* 32 */
};

struct omx_cmd_send_pull {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad;
	/* 8 */
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t local_rdma_id;
	/* 16 */
	uint32_t local_offset; /* FIXME: 64bits ? */
	uint32_t remote_rdma_id;
	/* 24 */
	uint32_t remote_offset; /* FIXME: 64bits ? */
	/* 28 */
};

struct omx_cmd_register_region {
	uint32_t nr_segments;
	uint32_t id;
	uint32_t seqnum;
	uint32_t pad;
	uint64_t memory_context;
	uint64_t segments;
};

struct omx_cmd_deregister_region {
	uint32_t id;
};

/************************
 * Event types
 */

#define OMX_EVT_NONE			0x00
#define OMX_EVT_SEND_MEDIUM_FRAG_DONE	0x01
#define OMX_EVT_RECV_TINY		0x12
#define OMX_EVT_RECV_SMALL		0x13
#define OMX_EVT_RECV_MEDIUM		0x14

static inline const char *
omx_strevt(unsigned type)
{
	switch (type) {
	case OMX_EVT_NONE:
		return "None";
	case OMX_EVT_SEND_MEDIUM_FRAG_DONE:
		return "Send Medium Fragment Done";
	case OMX_EVT_RECV_TINY:
		return "Receive Tiny";
	case OMX_EVT_RECV_SMALL:
		return "Receive Small";
	case OMX_EVT_RECV_MEDIUM:
		return "Receive Medium Fragment";
	default:
		return "** Unknown **";
	}
}

/************************
 * Event parameter types
 */

union omx_evt {
	/* generic event */
	struct omx_evt_generic {
		char pad[63];
		uint8_t type;
		/* 64 */
	} generic;

	/* send medium frag done */
	struct omx_evt_send_medium_frag_done {
		uint16_t sendq_page_offset;
		char pad[61];
		uint8_t type;
		/* 64 */
	} send_medium_frag_done;

	/* recv tiny */
	struct omx_evt_recv_tiny {
		uint64_t src_addr;
		/* 8 */
		uint8_t src_endpoint;
		uint8_t length;
		uint16_t seqnum;
		uint32_t pad1;
		/* 16 */
		uint64_t match_info;
		/* 24 */
		char data[OMX_TINY_MAX];
		/* 56 */
		uint8_t pad2[7];
		uint8_t type;
		/* 64 */
	} recv_tiny;

	/* recv small */
	struct omx_evt_recv_small {
		uint64_t src_addr;
		/* 8 */
		uint8_t src_endpoint;
		uint8_t pad1;
		uint16_t length;
		uint16_t seqnum;
		uint16_t pad2;
		/* 16 */
		uint64_t match_info;
		/* 24 */
		uint8_t pad3[39];
		uint8_t type;
		/* 64 */
	} recv_small;

	/* recv medium */
	struct omx_evt_recv_medium {
		uint64_t src_addr;
		/* 8 */
		uint8_t src_endpoint;
		uint8_t pad1;
		uint16_t seqnum;
		uint32_t msg_length;
		/* 16 */
		uint16_t frag_length;
		uint8_t frag_seqnum;
		uint8_t frag_pipeline;
		uint32_t pad2;
		/* 24 */
		uint64_t match_info;
		/* 32 */
		uint8_t pad3[31];
		uint8_t type;
		/* 64 */
	} recv_medium;
};

#endif /* __omx_io_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
