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

/* sendq: where the lib passes data to send to the driver */
#define OMX_SENDQ_ENTRY_SHIFT	12
#define OMX_SENDQ_ENTRY_SIZE	(1UL << OMX_SENDQ_ENTRY_SHIFT)
#define OMX_SENDQ_ENTRY_NR	1024
#define OMX_SENDQ_SIZE		(OMX_SENDQ_ENTRY_SIZE*OMX_SENDQ_ENTRY_NR)
#define OMX_SENDQ_FILE_OFFSET	0

/* recv: where the driver passes data received to the lib */
#define OMX_RECVQ_ENTRY_SHIFT	12
#define OMX_RECVQ_ENTRY_SIZE	(1UL << OMX_RECVQ_ENTRY_SHIFT)
#define OMX_RECVQ_ENTRY_NR	1024
#define OMX_RECVQ_SIZE		(OMX_RECVQ_ENTRY_SIZE*OMX_RECVQ_ENTRY_NR)
#define OMX_RECVQ_FILE_OFFSET	4096

/* expected eventq: where expected events are stored, medium send done and pull done */
/* unexpected eventq: where unexpected events are stored, incoming packets */
#define OMX_EVENTQ_ENTRY_SHIFT	6
#define OMX_EVENTQ_ENTRY_SIZE	(1UL << OMX_EVENTQ_ENTRY_SHIFT)
#define OMX_EXP_EVENTQ_ENTRY_NR	1024
#define OMX_UNEXP_EVENTQ_ENTRY_NR	1024
#define OMX_EXP_EVENTQ_SIZE		(OMX_EVENTQ_ENTRY_SIZE*OMX_EXP_EVENTQ_ENTRY_NR)
#define OMX_UNEXP_EVENTQ_SIZE		(OMX_EVENTQ_ENTRY_SIZE*OMX_UNEXP_EVENTQ_ENTRY_NR)
#define OMX_EXP_EVENTQ_FILE_OFFSET	(2*4096)
#define OMX_UNEXP_EVENTQ_FILE_OFFSET	(3*4096)

#define OMX_TINY_MAX		32
#define OMX_SMALL_MAX		128 /* at most 4096? FIXME: check that it fits in a linear skb and a recvq page */
#define OMX_MEDIUM_MAX		(8*4096)
#define OMX_RNDV_DATA_MAX	8
#define OMX_CONNECT_DATA_MAX	32

#define OMX_HOSTNAMELEN_MAX	80
#define OMX_IF_NAMESIZE		16

#define OMX_USER_REGION_MAX	256
typedef uint8_t omx_user_region_id_t;

struct omx_cmd_region_segment {
	uint64_t vaddr;
	uint32_t len;
	uint32_t pad;
};

/* driver desc */
struct omx_driver_desc {
	uint64_t jiffies;
	uint32_t hz;
	uint32_t board_max;
	uint32_t endpoint_max;
	uint32_t peer_max;
};

#define OMX_DRIVER_DESC_SIZE	sizeof(struct omx_driver_desc)
#define OMX_DRIVER_DESC_FILE_OFFSET	(4096*4096)

/************************
 * IOCTL commands
 */

#define OMX_CMD_BENCH			0x00
#define OMX_CMD_GET_BOARD_COUNT		0x04
#define OMX_CMD_GET_BOARD_ID		0x05
#define OMX_CMD_GET_ENDPOINT_INFO	0x06
#define OMX_CMD_PEERS_CLEAR		0x10
#define OMX_CMD_PEER_ADD		0x11
#define OMX_CMD_PEER_FROM_INDEX		0x12
#define OMX_CMD_PEER_FROM_ADDR		0x13
#define OMX_CMD_PEER_FROM_HOSTNAME	0x14
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
#define OMX_CMD_WAIT_EVENT		0x90

static inline const char *
omx_strcmd(unsigned cmd)
{
	switch (cmd) {
	case OMX_CMD_BENCH:
		return "Command Benchmark";
	case OMX_CMD_GET_BOARD_COUNT:
		return "Get Board Count";
	case OMX_CMD_GET_BOARD_ID:
		return "Get Board ID";
	case OMX_CMD_GET_ENDPOINT_INFO:
		return "Get Endpoint Info";
	case OMX_CMD_PEERS_CLEAR:
		return "Clear Peers";
	case OMX_CMD_PEER_ADD:
		return "Add Peer";
	case OMX_CMD_PEER_FROM_INDEX:
		return "Peer from Index";
	case OMX_CMD_PEER_FROM_ADDR:
		return "Peer from Addr";
	case OMX_CMD_PEER_FROM_HOSTNAME:
		return "Peer from Hostname";
	case OMX_CMD_OPEN_ENDPOINT:
		return "Open Endpoint";
	case OMX_CMD_CLOSE_ENDPOINT:
		return "Close Endpoint";
	case OMX_CMD_GET_ENDPOINT_SESSION_ID:
		return "Get Endpoint Session ID";
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
	case OMX_CMD_WAIT_EVENT:
		return "Wait Event";
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
#define OMX_CMD_BENCH_TYPE_RECV_NOTIFY	0x12
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
	char hostname[OMX_HOSTNAMELEN_MAX];
	char ifacename[OMX_IF_NAMESIZE];
};

struct omx_cmd_get_endpoint_info {
	uint32_t board_index;
	uint32_t endpoint_index;
	uint32_t closed;
	uint32_t pid;
	char command[32];
};

struct omx_cmd_misc_peer_info {
	uint64_t board_addr;
	char hostname[OMX_HOSTNAMELEN_MAX];
	uint32_t index;
};

struct omx_cmd_open_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;
};

struct omx_cmd_send_tiny {
	struct omx_cmd_send_tiny_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t pad1;
		uint32_t session_id;
		/* 8 */
		uint16_t seqnum;
		uint16_t piggyack;
		uint8_t length;
		uint8_t pad2[3];
		/* 16 */
		uint64_t match_info;
		/* 24 */
	} hdr;
	char data[OMX_TINY_MAX];
	/* 56 */
};

struct omx_cmd_send_small {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t pad1;
	uint32_t session_id;
	/* 8 */
	uint16_t seqnum;
	uint16_t piggyack;
	uint16_t length;
	uint16_t pad2;
	/* 16 */
	uint64_t vaddr;
	/* 24 */
	uint64_t match_info;
	/* 32 */
};

struct omx_cmd_send_medium {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t pad1;
	uint32_t session_id;
	/* 8 */
	uint16_t seqnum;
	uint16_t piggyack;
	uint16_t sendq_page_offset;
	uint16_t pad2;
	/* 16 */
	uint32_t msg_length;
	uint16_t frag_length;
	uint8_t frag_seqnum;
	uint8_t frag_pipeline;
	/* 24 */
	uint64_t match_info;
	/* 32 */
};

struct omx_cmd_send_rndv {
	struct omx_cmd_send_rndv_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t pad1;
		uint32_t session_id;
		/* 8 */
		uint16_t seqnum;
		uint16_t piggyack;
		uint8_t length;
		uint8_t pad2[3];
		/* 16 */
		uint64_t match_info;
		/* 24 */
	} hdr;
	char data[OMX_RNDV_DATA_MAX];
	/* 32 */
};

struct omx_cmd_send_connect {
	struct omx_cmd_send_connect_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t pad1;
		uint16_t seqnum;
		uint8_t length;
		uint8_t pad2;
		/* 8 */
	} hdr;
	char data[OMX_CONNECT_DATA_MAX];
	/* 40 */
};

struct omx_cmd_send_pull {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t pad1;
	uint32_t session_id;
	/* 8 */
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t lib_cookie;
	/* 16 */
	uint32_t local_rdma_id;
	uint32_t local_offset; /* FIXME: 64bits ? */
	/* 24 */
	uint32_t remote_rdma_id;
	uint32_t remote_offset; /* FIXME: 64bits ? */
	/* 32 */
};

struct omx_cmd_send_notify {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t pad1;
	uint32_t session_id;
	/* 8 */
	uint32_t total_length;
	uint16_t seqnum;
	uint16_t piggyack;
	/* 16 */
	uint8_t puller_rdma_id;
	uint8_t puller_rdma_seqnum;
	uint8_t pad2[6];
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

#define OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE	((uint32_t) -1)

#define OMX_CMD_WAIT_EVENT_STATUS_EVENT		0x01 /* some event arrived */
#define OMX_CMD_WAIT_EVENT_STATUS_INTR		0x02 /* interrupted by a signal without any event */
#define OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT	0x03 /* timeout expired without any event */
#define OMX_CMD_WAIT_EVENT_STATUS_RACE		0x04 /* some events arrived in the meantime, need to go back to user-space and check them first */

struct omx_cmd_wait_event {
	uint8_t status;
	uint32_t timeout; /* milliseconds */
	uint32_t next_exp_event_offset;
	uint32_t next_unexp_event_offset;
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
#define OMX_EVT_RECV_NACK_LIB		0x20

#define OMX_EVT_NACK_LIB_BAD_ENDPT	0x01
#define OMX_EVT_NACK_LIB_ENDPT_CLOSED	0x02
#define OMX_EVT_NACK_LIB_BAD_SESSION	0x03

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
	case OMX_EVT_RECV_NACK_LIB:
		return "Receive Nack Lib";
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
		uint32_t local_rdma_id;
		/* 8 */
		uint32_t pulled_length;
		uint32_t pad1;
		/* 16 */
		uint8_t pad2[47];
		uint8_t type;
		/* 64 */
	} pull_done;

	struct omx_evt_recv_connect {
		uint16_t peer_index;
		uint8_t src_endpoint;
		uint8_t pad1;
		/* 4 */
		uint16_t seqnum;
		uint8_t length;
		uint8_t pad2;
		/* 8 */
		uint8_t data[OMX_CONNECT_DATA_MAX];
		/* 40 */
		uint8_t pad3[23];
		uint8_t type;
	} recv_connect;

	struct omx_evt_recv_nack_lib {
		uint16_t peer_index;
		uint8_t src_endpoint;
		uint8_t nack_type;
		uint16_t seqnum;
		uint16_t pad1;
		/* 8 */
		uint8_t pad3[55];
		uint8_t type;
	} recv_nack_lib;

	struct omx_evt_recv_msg {
		uint16_t peer_index;
		uint8_t src_endpoint;
		uint8_t pad1;
		uint16_t seqnum;
		uint16_t piggyack;
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
				uint32_t recvq_offset;
				uint16_t length;
				uint16_t pad[17];
				/* 40 */
			} small;

			struct {
				uint32_t recvq_offset;
				uint32_t msg_length;
				/* 8 */
				uint16_t frag_length;
				uint8_t frag_seqnum;
				uint8_t frag_pipeline;
				/* 12 */
				uint32_t pad[7];
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
