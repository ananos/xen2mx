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
#define OMX_MEDIUM_MAX		65536
#define OMX_RNDV_DATA_MAX	8
#define OMX_CONNECT_DATA_MAX	32

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

#define OMX_CMD_BENCH			0x00
#define OMX_CMD_GET_BOARD_MAX		0x01
#define OMX_CMD_GET_ENDPOINT_MAX	0x02
#define OMX_CMD_GET_PEER_MAX		0x03
#define OMX_CMD_GET_BOARD_COUNT		0x04
#define OMX_CMD_GET_BOARD_ID		0x05
#define OMX_CMD_OPEN_ENDPOINT		0x71
#define OMX_CMD_CLOSE_ENDPOINT		0x72
#define OMX_CMD_GET_ENDPOINT_SESSION_ID	0x73
#define OMX_CMD_SEND_TINY		0x81
#define OMX_CMD_SEND_SMALL		0x82
#define OMX_CMD_SEND_MEDIUM		0x83
#define OMX_CMD_SEND_RNDV		0x84
#define OMX_CMD_SEND_PULL		0x85
#define OMX_CMD_SEND_NOTIFY		0x86
#define OMX_CMD_REGISTER_REGION		0x87
#define OMX_CMD_DEREGISTER_REGION	0x88
#define OMX_CMD_SEND_CONNECT		0x89

static inline const char *
omx_strcmd(unsigned cmd)
{
	switch (cmd) {
	case OMX_CMD_BENCH:
		return "Command Benchmark";
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
	case OMX_CMD_GET_ENDPOINT_SESSION_ID:
		return "Get Endpoint Session ID";
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
	case OMX_CMD_SEND_RNDV:
		return "Send Rendez-vous";
	case OMX_CMD_SEND_PULL:
		return "Send Pull";
	case OMX_CMD_SEND_NOTIFY:
		return "Send Notify";
	case OMX_CMD_REGISTER_REGION:
		return "Register Region";
	case OMX_CMD_DEREGISTER_REGION:
		return "Deregister Region";
	case OMX_CMD_SEND_CONNECT:
		return "Send Connect";
	default:
		return "** Unknown **";
	}
}

/************************
 * IOCTL parameter types
 */

/* level 0 testing, only pass the command and get the endpoint, no parameter given */
#define OMX_CMD_BENCH_TYPE_PARAMS	0x01
#define OMX_CMD_BENCH_TYPE_SEND_ALLOC	0x02
#define OMX_CMD_BENCH_TYPE_SEND_PREP	0x03
#define OMX_CMD_BENCH_TYPE_SEND_FILL	0x04
#define OMX_CMD_BENCH_TYPE_SEND_DONE	0x05
#define OMX_CMD_BENCH_TYPE_RECV_ACQU	0x11
#define OMX_CMD_BENCH_TYPE_RECV_ALLOC	0x12
#define OMX_CMD_BENCH_TYPE_RECV_DONE	0x13

struct omx_cmd_bench {
	struct omx_cmd_bench_hdr {
		uint8_t type;
		uint8_t pad[7];
		/* 8 */
	} hdr;
	char dummy_data[OMX_TINY_MAX];
	/* 40 */
};

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
		uint16_t dest_src_peer_index;
		uint16_t seqnum;
		uint32_t session_id;
		/* 16 */
		uint64_t match_info;
		/* 24 */
	} hdr;
	char data[OMX_TINY_MAX];
	/* 56 */
};

struct omx_cmd_send_small {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad1;
	/* 8 */
	uint16_t dest_src_peer_index;
	uint16_t pad2;
	uint32_t session_id;
	/* 16 */
	uint16_t length;
	uint16_t seqnum;
	uint32_t pad3;
	/* 24 */
	uint64_t vaddr;
	uint64_t match_info;
	/* 40 */
};

struct omx_cmd_send_medium {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad1;
	/* 8 */
	uint16_t dest_src_peer_index;
	uint16_t pad2;
	uint32_t session_id;
	/* 16 */
	uint16_t seqnum;
	uint16_t sendq_page_offset;
	uint32_t pad3;
	/* 24 */
	uint32_t msg_length;
	uint16_t frag_length;
	uint8_t frag_seqnum;
	uint8_t frag_pipeline;
	/* 32 */
	uint64_t match_info;
	/* 40 */
};

struct omx_cmd_send_rndv {
	struct omx_cmd_send_rndv_hdr {
		uint64_t dest_addr;
		uint8_t dest_endpoint;
		uint8_t length;
		/* 8 */
		uint16_t dest_src_peer_index;
		uint16_t seqnum;
		uint32_t session_id;
		/* 16 */
		uint64_t match_info;
		/* 24 */
	} hdr;
	char data[OMX_RNDV_DATA_MAX];
	/* 56 */
};

struct omx_cmd_send_connect {
	struct omx_cmd_send_connect_hdr {
		uint64_t dest_addr;
		uint8_t dest_endpoint;
		uint8_t length;
		/* 8 */
		uint16_t seqnum;
		uint16_t src_dest_peer_index;
		/* 12 */
	} hdr;
	char data[OMX_CONNECT_DATA_MAX];
};

struct omx_cmd_send_pull {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad;
	/* 8 */
	uint32_t session_id;
	uint32_t length; /* FIXME: 64bits ? */
	/* 16 */
	uint32_t local_rdma_id;
	uint32_t local_offset; /* FIXME: 64bits ? */
	/* 24 */
	uint32_t remote_rdma_id;
	uint32_t remote_offset; /* FIXME: 64bits ? */
	/* 32 */
	uint32_t lib_cookie;
	/* 36 */
};

struct omx_cmd_send_notify {
	uint64_t dest_addr;
	uint8_t dest_endpoint;
	uint8_t pad1;
	/* 8 */
	uint16_t dest_src_peer_index;
	uint16_t pad2;
	uint32_t session_id;
	/* 16 */
	uint32_t total_length;
	uint8_t puller_rdma_id;
	uint8_t puller_rdma_seqnum;
	uint16_t seqnum;
	/* 24 */
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
#define OMX_EVT_PULL_DONE		0x02
#define OMX_EVT_RECV_CONNECT		0x11
#define OMX_EVT_RECV_TINY		0x12
#define OMX_EVT_RECV_SMALL		0x13
#define OMX_EVT_RECV_MEDIUM		0x14
#define OMX_EVT_RECV_RNDV		0x15
#define OMX_EVT_RECV_NOTIFY		0x16

static inline const char *
omx_strevt(unsigned type)
{
	switch (type) {
	case OMX_EVT_NONE:
		return "None";
	case OMX_EVT_SEND_MEDIUM_FRAG_DONE:
		return "Send Medium Fragment Done";
	case OMX_EVT_PULL_DONE:
		return "Pull Done";
	case OMX_EVT_RECV_CONNECT:
		return "Receive Connect";
	case OMX_EVT_RECV_TINY:
		return "Receive Tiny";
	case OMX_EVT_RECV_SMALL:
		return "Receive Small";
	case OMX_EVT_RECV_MEDIUM:
		return "Receive Medium Fragment";
	case OMX_EVT_RECV_RNDV:
		return "Receive Rendez-vous";
	case OMX_EVT_RECV_NOTIFY:
		return "Receive Notify";
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

	struct omx_evt_pull_done {
		uint32_t lib_cookie;
		uint32_t pulled_length;
		/* 8 */
		uint32_t local_rdma_id;
		uint32_t pad1;
		/* 16 */
		uint8_t pad2[47];
		uint8_t type;
		/* 64 */
	} pull_done;

	struct omx_evt_recv_connect {
		uint64_t src_addr;
		/* 8 */
		uint16_t src_dest_peer_index;
		uint8_t src_endpoint;
		uint8_t pad1;
		/* 12 */
		uint16_t seqnum;
		uint8_t length;
		uint8_t pad2;
		/* 16 */
		uint8_t data[OMX_CONNECT_DATA_MAX];
		/* 48 */
		uint8_t pad3[15];
		uint8_t type;
	} recv_connect;

	struct omx_evt_recv_msg {
		uint16_t dest_src_peer_index;
		uint8_t src_endpoint;
		uint8_t pad1;
		uint16_t seqnum;
		uint16_t pad2;
		/* 8 */
		uint64_t match_info;
		/* 16 */
		union {
			struct {
				uint8_t length;
				uint8_t pad[7];
				/* 8 */
				char data[OMX_TINY_MAX];
				/* 40 */
			} tiny;

			struct {
				uint16_t length;
				uint16_t pad[19];
				/* 40 */
			} small;

			struct {
				uint32_t msg_length;
				uint16_t frag_length;
				uint8_t frag_seqnum;
				uint8_t frag_pipeline;
				/* 8 */
				uint64_t pad[4];
				/* 40 */
			} medium;

			struct {
				uint8_t length;
				uint8_t pad1[7];
				/* 8 */
				char data[OMX_RNDV_DATA_MAX];
				/* 16 */
				uint64_t pad2[3];
				/* 40 */
			} rndv;

			struct {
				uint32_t length;
				uint8_t puller_rdma_id;
				uint8_t puller_rdma_seqnum;
				uint16_t pad1;
				/* 8 */
				uint64_t pad2[4];
				/* 40 */
			} notify;

			/* 40 */;
		} specific;
		/* 56 */
		uint8_t pad3[7];
		uint8_t type;
		/* 64 */
	} recv_msg;

};

#endif /* __omx_io_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
