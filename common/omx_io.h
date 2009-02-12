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

#ifndef __omx_io_h__
#define __omx_io_h__

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

#include "omx_wire.h"

/*
 * The ABI version should be increased when ioctl commands are added
 * or modified, or when the user-mapped driver- and endpoint-descriptors
 * are modified.
 */
#define OMX_DRIVER_ABI_VERSION		0x204

/************************
 * Common parameters or IOCTL subtypes
 */

/* number of slots in the sendq and recvq */
#define OMX_SENDQ_ENTRY_NR	1024
#define OMX_RECVQ_ENTRY_NR	1024

/* expected eventq: where expected events are stored, medium send done and pull done */
/* unexpected eventq: where unexpected events are stored, incoming packets */
#define OMX_EVENTQ_ENTRY_SHIFT	6
#define OMX_EVENTQ_ENTRY_SIZE	(1UL << OMX_EVENTQ_ENTRY_SHIFT)
#define OMX_EXP_EVENTQ_ENTRY_NR	1024
#define OMX_UNEXP_EVENTQ_ENTRY_NR	1024
#define OMX_EXP_EVENTQ_SIZE		(OMX_EVENTQ_ENTRY_SIZE * OMX_EXP_EVENTQ_ENTRY_NR)
#define OMX_UNEXP_EVENTQ_SIZE		(OMX_EVENTQ_ENTRY_SIZE * OMX_UNEXP_EVENTQ_ENTRY_NR)

#define OMX_TINY_MAX		32
#define OMX_SMALL_MAX		128
#define OMX_MEDIUM_MAX		32768
#define OMX_RNDV_DATA_MAX	8
#define OMX_CONNECT_DATA_MAX	32
#define OMX_TRUC_DATA_MAX	48

#define OMX_HOSTNAMELEN_MAX	80
#define OMX_IF_NAMESIZE		16
#define OMX_DRIVER_NAMESIZE	16
#define OMX_COMMAND_LEN_MAX	32

#define OMX_RAW_PKT_LEN_MAX	1024
#define OMX_RAW_RECVQ_LEN	32
#define OMX_RAW_ENDPOINT_INDEX	255

#define OMX_USER_REGION_MAX	256
typedef uint8_t omx_user_region_id_t;

struct omx_cmd_user_segment {
	uint64_t vaddr;
	/* 8 */
	uint64_t len;
	/* 16 */
};

#define OMX_ABI_CONFIG_WIRECOMPAT		(1<<0)
#define OMX_ABI_CONFIG_ENDIANCOMPAT		(1<<1)

/* driver desc */
struct omx_driver_desc {
	uint32_t abi_version;
	uint32_t abi_config;
	/* 8 */
	uint32_t features;
	uint32_t pad0;
	/* 16 */
	uint64_t jiffies;
	/* 24 */
	uint32_t hz;
	uint16_t mtu;
	uint8_t packet_ring_entry_shift;
	uint8_t pad1;
	/* 32 */
	uint32_t board_max;
	uint32_t endpoint_max;
	/* 40 */
	uint32_t peer_max;
	uint32_t peer_table_size;
	/* 48 */
	uint32_t peer_table_configured;
	uint32_t peer_table_version;
	/* 56 */
	uint64_t peer_table_mapper_id;
	/* 64 */
};

static inline uint32_t
omx_get_abi_config(void) {
	uint32_t val = 0;
#ifdef OMX_MX_WIRE_COMPAT
	val |= OMX_ABI_CONFIG_WIRECOMPAT;
#endif
#ifdef OMX_ENDIAN_COMPAT
	val |= OMX_ABI_CONFIG_ENDIANCOMPAT;
#endif
	return val;
}

#define OMX_DRIVER_DESC_SIZE	sizeof(struct omx_driver_desc)

#define OMX_DRIVER_FEATURE_SHARED		(1<<1)
#define OMX_DRIVER_FEATURE_PIN_INVALIDATE	(1<<2)

/* endpoint desc */
struct omx_endpoint_desc {
	uint64_t status;
	/* 8 */
	uint64_t wakeup_jiffies;
	/* 16 */
	uint32_t session_id;
	uint32_t user_event_index;
	/* 24 */
};

#define OMX_ENDPOINT_DESC_SIZE	sizeof(struct omx_endpoint_desc)

/* fake mmap file offsets (anything unique, multiple of page size) */
#define OMX_SENDQ_FILE_OFFSET		0
#define OMX_RECVQ_FILE_OFFSET		(1024*1024)
#define OMX_EXP_EVENTQ_FILE_OFFSET	(2*1024*1024)
#define OMX_UNEXP_EVENTQ_FILE_OFFSET	(3*1024*1024)
#define OMX_DRIVER_DESC_FILE_OFFSET	(4*1024*1024)
#define OMX_ENDPOINT_DESC_FILE_OFFSET	(5*1024*1024)

#define OMX_NO_WAKEUP_JIFFIES 0

#define OMX_ENDPOINT_DESC_STATUS_EXP_EVENTQ_FULL (1ULL << 0)
#define OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL (1ULL << 1)
#define OMX_ENDPOINT_DESC_STATUS_IFACE_DOWN (1ULL << 2)
#define OMX_ENDPOINT_DESC_STATUS_IFACE_BAD_MTU (1ULL << 3)
#define OMX_ENDPOINT_DESC_STATUS_IFACE_REMOVED (1ULL << 4)
#define OMX_ENDPOINT_DESC_STATUS_IFACE_HIGH_INTRCOAL (1ULL << 5)

/* only valid for get_info and get_counters */
#define OMX_SHARED_FAKE_IFACE_INDEX 0xfffffffe

/************************
 * IOCTL parameter types
 */

struct omx_cmd_get_board_info {
	uint32_t board_index;
	uint32_t pad;
	/* 8 */
	struct omx_board_info {
		uint64_t addr;
		/* 8 */
		uint32_t numa_node;
		uint32_t pad;
		/* 16 */
		char hostname[OMX_HOSTNAMELEN_MAX];
		/* 96 */
		char ifacename[OMX_IF_NAMESIZE];
		/* 112 */
		char drivername[OMX_DRIVER_NAMESIZE];
		/* 128 */
	} info;
	/* 136 */
};

struct omx_cmd_get_endpoint_info {
	uint32_t board_index;
	uint32_t endpoint_index;
	/* 8 */
	struct omx_endpoint_info {
		uint32_t closed;
		uint32_t pid;
		/* 8 */
		char command[OMX_COMMAND_LEN_MAX];
		/* 40 */
	} info;
	/* 48 */
};

struct omx_cmd_get_counters {
	uint32_t board_index;
	uint8_t clear;
	uint8_t pad1[3];
	/* 8 */
	uint64_t buffer_addr;
	/* 16 */
	uint32_t buffer_length;
	uint32_t pad2;
	/* 24 */
};

struct omx_cmd_set_hostname {
	uint32_t board_index;
	uint32_t pad;
	/* 8 */
	char hostname[OMX_HOSTNAMELEN_MAX];
	/* 88 */
};

struct omx_cmd_misc_peer_info {
	uint64_t board_addr;
	/* 8 */
	char hostname[OMX_HOSTNAMELEN_MAX];
	/* 88 */
	uint32_t index;
	uint32_t pad;
	/* 96 */
};

struct omx_cmd_peer_table_state {
	uint32_t configured;
	uint32_t version;
	/* 8 */
	uint32_t size;
	uint32_t pad;
	/* 16 */
	uint64_t mapper_id;
	/* 24 */
};

struct omx_cmd_raw_open_endpoint {
	uint8_t board_index;
	uint8_t pad[7];
};

struct omx_cmd_raw_send {
	uint64_t buffer;
	uint32_t buffer_length;
	uint32_t need_event;
	uint64_t context;
};

#define OMX_CMD_RAW_NO_EVENT	       	0
#define OMX_CMD_RAW_EVENT_SEND_COMPLETE	1
#define OMX_CMD_RAW_EVENT_RECV_COMPLETE	2

struct omx_cmd_raw_get_event {
	uint64_t buffer;
	uint32_t buffer_length;
	uint32_t timeout;
	uint64_t context;
	uint32_t status;
	uint32_t pad;
};

struct omx_cmd_open_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;
	uint8_t pad[6];
	/* 8 */
};

struct omx_cmd_send_tiny {
	struct omx_cmd_send_tiny_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t shared;
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
	/* 24 */
	char data[OMX_TINY_MAX];
	/* 56 */
};

struct omx_cmd_send_small {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t shared;
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

struct omx_cmd_send_mediumsq_frag {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t shared;
	uint32_t session_id;
	/* 8 */
	uint16_t seqnum;
	uint16_t piggyack;
	uint32_t sendq_offset;
	/* 16 */
	uint32_t msg_length;
	uint16_t frag_length;
	uint8_t frag_seqnum;
	uint8_t frag_pipeline;
	/* 24 */
	uint64_t match_info;
	/* 32 */
};

struct omx_cmd_send_mediumva {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t shared;
	uint32_t session_id;
	/* 8 */
	uint16_t seqnum;
	uint16_t piggyack;
	uint32_t length;
	/* 16 */
	uint32_t pad;
	uint32_t nr_segments;
	/* 24 */
	uint64_t segments;
	/* 32 */
	uint64_t match_info;
	/* 40 */
};

struct omx_cmd_send_rndv {
	struct omx_cmd_send_rndv_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t shared;
		uint32_t session_id;
		/* 8 */
		uint16_t seqnum;
		uint16_t piggyack;
		uint8_t length;
		uint8_t pad1[3];
		/* 16 */
		uint8_t user_region_id_needed;
		uint8_t pad2[3];
		uint32_t user_region_length_needed;
		/* 24 */
		uint64_t match_info;
		/* 32 */
	} hdr;
	/* 32 */
	char data[OMX_RNDV_DATA_MAX];
	/* 40 */
};

struct omx_cmd_send_connect {
	struct omx_cmd_send_connect_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t shared_disabled;
		uint16_t seqnum;
		uint8_t length;
		uint8_t pad2;
		/* 8 */
	} hdr;
	/* 8 */
	char data[OMX_CONNECT_DATA_MAX];
	/* 40 */
};

struct omx_cmd_pull {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t shared;
	uint32_t session_id;
	/* 8 */
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t resend_timeout_jiffies;
	/* 16 */
	uint32_t local_rdma_id;
	uint32_t remote_offset; /* FIXME: 64bits ? */
	/* 24 */
	uint32_t remote_rdma_id;
	uint32_t remote_rdma_seqnum;
	/* 32 */
	uint64_t lib_cookie;
	/* 40 */
};

struct omx_cmd_send_notify {
	uint16_t peer_index;
	uint8_t dest_endpoint;
	uint8_t shared;
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

struct omx_cmd_send_truc {
	struct omx_cmd_send_truc_hdr {
		uint16_t peer_index;
		uint8_t dest_endpoint;
		uint8_t shared;
		uint32_t session_id;
		/* 8 */
		uint8_t length;
		uint8_t pad[7];
		/* 16 */
	} hdr;
	/* 16 */
	char data[OMX_TRUC_DATA_MAX];
	/* 48 */
};

struct omx_cmd_create_user_region {
	uint32_t nr_segments;
	uint32_t id;
	/* 8 */
	uint32_t seqnum;
	uint32_t pad;
	/* 16 */
	uint64_t memory_context;
	/* 24 */
	uint64_t segments;
	/* 32 */
};

struct omx_cmd_destroy_user_region {
	uint32_t id;
	uint32_t pad;
	/* 8 */
};

#define OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE	((uint64_t) -1)

#define OMX_CMD_WAIT_EVENT_STATUS_NONE		0x00 /* nothing happen, should not be reported in user-space */
#define OMX_CMD_WAIT_EVENT_STATUS_EVENT		0x01 /* some event arrived */
#define OMX_CMD_WAIT_EVENT_STATUS_INTR		0x02 /* interrupted by a signal without any event */
#define OMX_CMD_WAIT_EVENT_STATUS_PROGRESS	0x03 /* wake up because of retransmission */
#define OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT	0x04 /* timeout expired without any event */
#define OMX_CMD_WAIT_EVENT_STATUS_RACE		0x05 /* some events arrived in the meantime, need to go back to user-space and check them first */
#define OMX_CMD_WAIT_EVENT_STATUS_WAKEUP	0x06 /* the application called the wakeup ioctl */

struct omx_cmd_wait_event {
	uint8_t status;
	uint8_t pad[3];
	/* 4 */
	uint32_t user_event_index;
	uint32_t next_exp_event_offset;
	uint32_t next_unexp_event_offset;
	/* 16 */
	uint64_t jiffies_expire; /* absolute jiffies where to wakeup, or OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE */
	/* 24 */
};

struct omx_cmd_wakeup {
	uint32_t status;
	uint32_t pad;
	/* 8 */
};

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
	/* 8 */
	char dummy_data[OMX_TINY_MAX];
	/* 40 */
};

/************************
 * IOCTL commands
 */

#define OMX_CMD_MAGIC	'O'
#define OMX_CMD_INDEX(x)	_IOC_NR(x)

#define OMX_CMD_GET_BOARD_COUNT		_IOW(OMX_CMD_MAGIC, 0x11, uint32_t)
#define OMX_CMD_GET_BOARD_INFO		_IOWR(OMX_CMD_MAGIC, 0x12, struct omx_cmd_get_board_info)
#define OMX_CMD_GET_ENDPOINT_INFO	_IOWR(OMX_CMD_MAGIC, 0x13, struct omx_cmd_get_endpoint_info)
#define OMX_CMD_GET_COUNTERS		_IOWR(OMX_CMD_MAGIC, 0x14, struct omx_cmd_get_counters)
#define OMX_CMD_SET_HOSTNAME		_IOR(OMX_CMD_MAGIC, 0x15, struct omx_cmd_set_hostname)
#define OMX_CMD_PEER_TABLE_SET_STATE	_IOW(OMX_CMD_MAGIC, 0x20, struct omx_cmd_peer_table_state)
#define OMX_CMD_PEER_TABLE_CLEAR	_IO(OMX_CMD_MAGIC, 0x21)
#define OMX_CMD_PEER_TABLE_CLEAR_NAMES	_IO(OMX_CMD_MAGIC, 0x22)
#define OMX_CMD_PEER_ADD		_IOR(OMX_CMD_MAGIC, 0x23, struct omx_cmd_misc_peer_info)
#define OMX_CMD_PEER_FROM_INDEX		_IOWR(OMX_CMD_MAGIC, 0x24, struct omx_cmd_misc_peer_info)
#define OMX_CMD_PEER_FROM_ADDR		_IOWR(OMX_CMD_MAGIC, 0x25, struct omx_cmd_misc_peer_info)
#define OMX_CMD_PEER_FROM_HOSTNAME	_IOWR(OMX_CMD_MAGIC, 0x26, struct omx_cmd_misc_peer_info)
#define OMX_CMD_RAW_OPEN_ENDPOINT	_IOR(OMX_CMD_MAGIC, 0x30, struct omx_cmd_raw_open_endpoint)
#define OMX_CMD_RAW_SEND		_IOR(OMX_CMD_MAGIC, 0x31, struct omx_cmd_raw_send)
#define OMX_CMD_RAW_GET_EVENT		_IOWR(OMX_CMD_MAGIC, 0x32, struct omx_cmd_raw_get_event)
#define OMX_CMD_OPEN_ENDPOINT		_IOR(OMX_CMD_MAGIC, 0x71, struct omx_cmd_open_endpoint)
/* WARNING: endpoint-based cmd numbers must start at OMX_CMD_BENCH and remain consecutive */
#define OMX_CMD_BENCH			_IOR(OMX_CMD_MAGIC, 0x80, struct omx_cmd_bench)
#define OMX_CMD_SEND_TINY		_IOR(OMX_CMD_MAGIC, 0x81, struct omx_cmd_send_tiny)
#define OMX_CMD_SEND_SMALL		_IOR(OMX_CMD_MAGIC, 0x82, struct omx_cmd_send_small)
#define OMX_CMD_SEND_MEDIUMSQ_FRAG	_IOR(OMX_CMD_MAGIC, 0x83, struct omx_cmd_send_mediumsq_frag)
#define OMX_CMD_SEND_RNDV		_IOR(OMX_CMD_MAGIC, 0x84, struct omx_cmd_send_rndv)
#define OMX_CMD_PULL			_IOR(OMX_CMD_MAGIC, 0x85, struct omx_cmd_pull)
#define OMX_CMD_SEND_NOTIFY		_IOR(OMX_CMD_MAGIC, 0x86, struct omx_cmd_send_notify)
#define OMX_CMD_SEND_CONNECT		_IOR(OMX_CMD_MAGIC, 0x87, struct omx_cmd_send_connect)
#define OMX_CMD_SEND_TRUC		_IOR(OMX_CMD_MAGIC, 0x88, struct omx_cmd_send_truc)
#define OMX_CMD_CREATE_USER_REGION	_IOR(OMX_CMD_MAGIC, 0x89, struct omx_cmd_create_user_region)
#define OMX_CMD_DESTROY_USER_REGION	_IOR(OMX_CMD_MAGIC, 0x8a, struct omx_cmd_destroy_user_region)
#define OMX_CMD_WAIT_EVENT		_IOWR(OMX_CMD_MAGIC, 0x8b, struct omx_cmd_wait_event)
#define OMX_CMD_WAKEUP			_IOR(OMX_CMD_MAGIC, 0x8c, struct omx_cmd_wakeup)
#define OMX_CMD_SEND_MEDIUMVA		_IOR(OMX_CMD_MAGIC, 0x8d, struct omx_cmd_send_mediumva)

static inline const char *
omx_strcmd(unsigned cmd)
{
	switch (cmd) {
	case OMX_CMD_GET_BOARD_COUNT:
		return "Get Board Count";
	case OMX_CMD_GET_BOARD_INFO:
		return "Get Board Info";
	case OMX_CMD_GET_ENDPOINT_INFO:
		return "Get Endpoint Info";
	case OMX_CMD_GET_COUNTERS:
		return "Get Counters";
	case OMX_CMD_SET_HOSTNAME:
		return "Set Hostname";
	case OMX_CMD_PEER_TABLE_SET_STATE:
		return "Set Peer Table State";
	case OMX_CMD_PEER_TABLE_CLEAR:
		return "Clear Peer Table";
	case OMX_CMD_PEER_TABLE_CLEAR_NAMES:
		return "Clear Names in Peer Table";
	case OMX_CMD_PEER_ADD:
		return "Add Peer";
	case OMX_CMD_PEER_FROM_INDEX:
		return "Peer from Index";
	case OMX_CMD_PEER_FROM_ADDR:
		return "Peer from Addr";
	case OMX_CMD_PEER_FROM_HOSTNAME:
		return "Peer from Hostname";
	case OMX_CMD_RAW_OPEN_ENDPOINT:
		return "Open Raw Endpoint";
	case OMX_CMD_RAW_SEND:
		return "Raw Send";
	case OMX_CMD_RAW_GET_EVENT:
		return "Raw Get Event";
	case OMX_CMD_OPEN_ENDPOINT:
		return "Open Endpoint";
	case OMX_CMD_BENCH:
		return "Command Benchmark";
	case OMX_CMD_SEND_TINY:
		return "Send Tiny";
	case OMX_CMD_SEND_SMALL:
		return "Send Small";
	case OMX_CMD_SEND_MEDIUMSQ_FRAG:
		return "Send MediumSQ Fragment";
	case OMX_CMD_SEND_MEDIUMVA:
		return "Send MediumVA";
	case OMX_CMD_SEND_RNDV:
		return "Send Rendez-vous";
	case OMX_CMD_PULL:
		return "Pull";
	case OMX_CMD_SEND_NOTIFY:
		return "Send Notify";
	case OMX_CMD_SEND_CONNECT:
		return "Send Connect";
	case OMX_CMD_SEND_TRUC:
		return "Send Truc";
	case OMX_CMD_CREATE_USER_REGION:
		return "Create User Region";
	case OMX_CMD_DESTROY_USER_REGION:
		return "Destroy User Region";
	case OMX_CMD_WAIT_EVENT:
		return "Wait Event";
	case OMX_CMD_WAKEUP:
		return "Wakeup";
	default:
		return "** Unknown **";
	}
}

/************************
 * Event types
 */

#define OMX_EVT_NONE			0x00
#define OMX_EVT_IGNORE			0x01
#define OMX_EVT_RECV_CONNECT		0x11
#define OMX_EVT_RECV_TINY		0x12
#define OMX_EVT_RECV_SMALL		0x13
#define OMX_EVT_RECV_MEDIUM_FRAG	0x14
#define OMX_EVT_RECV_RNDV		0x15
#define OMX_EVT_RECV_NOTIFY		0x16
#define OMX_EVT_RECV_TRUC		0x17
#define OMX_EVT_RECV_NACK_LIB		0x18
#define OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE	0x20
#define OMX_EVT_PULL_DONE		0x21

#define OMX_EVT_NACK_LIB_BAD_ENDPT	0x01
#define OMX_EVT_NACK_LIB_ENDPT_CLOSED	0x02
#define OMX_EVT_NACK_LIB_BAD_SESSION	0x03

#define OMX_EVT_PULL_DONE_SUCCESS	0x00
#define OMX_EVT_PULL_DONE_BAD_ENDPT	0x01
#define OMX_EVT_PULL_DONE_ENDPT_CLOSED	0x02
#define OMX_EVT_PULL_DONE_BAD_SESSION	0x03
#define OMX_EVT_PULL_DONE_BAD_RDMAWIN	0x04
#define OMX_EVT_PULL_DONE_ABORTED	0x05
#define OMX_EVT_PULL_DONE_TIMEOUT	0x06

static inline const char *
omx_strevt(unsigned type)
{
	switch (type) {
	case OMX_EVT_NONE:
		return "None";
	case OMX_EVT_IGNORE:
		return "Ignore";
	case OMX_EVT_RECV_CONNECT:
		return "Receive Connect";
	case OMX_EVT_RECV_TINY:
		return "Receive Tiny";
	case OMX_EVT_RECV_SMALL:
		return "Receive Small";
	case OMX_EVT_RECV_MEDIUM_FRAG:
		return "Receive Medium Fragment";
	case OMX_EVT_RECV_RNDV:
		return "Receive Rendez-vous";
	case OMX_EVT_RECV_NOTIFY:
		return "Receive Notify";
	case OMX_EVT_RECV_TRUC:
		return "Receive Truc";
	case OMX_EVT_RECV_NACK_LIB:
		return "Receive Nack Lib";
	case OMX_EVT_SEND_MEDIUMSQ_FRAG_DONE:
		return "Send MediumSQ Fragment Done";
	case OMX_EVT_PULL_DONE:
		return "Pull Done";
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
	struct omx_evt_send_mediumsq_frag_done {
		uint32_t sendq_offset;
		char pad[59];
		uint8_t type;
		/* 64 */
	} send_mediumsq_frag_done;

	struct omx_evt_pull_done {
		uint64_t lib_cookie;
		/* 8 */
		uint32_t local_rdma_id;
		uint8_t status;
		uint8_t pad1[3];
		/* 16 */
		uint8_t pad2[47];
		uint8_t type;
		/* 64 */
	} pull_done;

	struct omx_evt_recv_connect {
		uint16_t peer_index;
		uint8_t src_endpoint;
		uint8_t shared;
		uint16_t seqnum;
		uint8_t length;
		uint8_t pad2;
		/* 8 */
		uint8_t data[OMX_CONNECT_DATA_MAX];
		/* 40 */
		uint8_t pad3[23];
		uint8_t type;
		/* 64 */
	} recv_connect;

	struct omx_evt_recv_truc {
		uint16_t peer_index;
		uint8_t src_endpoint;
		uint8_t length;
		uint8_t pad2[4];
		/* 8 */
		char data[OMX_TRUC_DATA_MAX];
		/* 56 */
		uint8_t pad3[7];
		uint8_t type;
		/* 64 */
	} recv_truc;

	struct omx_evt_recv_nack_lib {
		uint16_t peer_index;
		uint8_t src_endpoint;
		uint8_t nack_type;
		uint16_t seqnum;
		uint16_t pad1;
		/* 8 */
		uint8_t pad3[55];
		uint8_t type;
		/* 64 */
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
			} medium_frag;

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

/***********
 * Counters
 */

enum omx_counter_index {
	OMX_COUNTER_SEND_TINY = 0,
	OMX_COUNTER_SEND_SMALL,
	OMX_COUNTER_SEND_MEDIUMSQ_FRAG,
	OMX_COUNTER_SEND_MEDIUMVA_FRAG,
	OMX_COUNTER_SEND_RNDV,
	OMX_COUNTER_SEND_NOTIFY,
	OMX_COUNTER_SEND_CONNECT,
	OMX_COUNTER_SEND_TRUC,
	OMX_COUNTER_SEND_NACK_LIB,
	OMX_COUNTER_SEND_NACK_MCP,
	OMX_COUNTER_SEND_PULL_REQ,
	OMX_COUNTER_SEND_PULL_REPLY,
	OMX_COUNTER_SEND_RAW,
	OMX_COUNTER_SEND_HOST_QUERY,
	OMX_COUNTER_SEND_HOST_REPLY,

	OMX_COUNTER_RECV_TINY,
	OMX_COUNTER_RECV_SMALL,
	OMX_COUNTER_RECV_MEDIUM_FRAG,
	OMX_COUNTER_RECV_RNDV,
	OMX_COUNTER_RECV_NOTIFY,
	OMX_COUNTER_RECV_CONNECT,
	OMX_COUNTER_RECV_TRUC,
	OMX_COUNTER_RECV_NACK_LIB,
	OMX_COUNTER_RECV_NACK_MCP,
	OMX_COUNTER_RECV_PULL_REQ,
	OMX_COUNTER_RECV_PULL_REPLY,
	OMX_COUNTER_RECV_RAW,
	OMX_COUNTER_RECV_HOST_QUERY,
	OMX_COUNTER_RECV_HOST_REPLY,

	OMX_COUNTER_DMARECV_MEDIUM_FRAG,
	OMX_COUNTER_DMARECV_PARTIAL_MEDIUM_FRAG,
	OMX_COUNTER_DMARECV_PULL_REPLY,
	OMX_COUNTER_DMARECV_PARTIAL_PULL_REPLY,
	OMX_COUNTER_DMARECV_PULL_REPLY_WAIT_DEFERRED,

	OMX_COUNTER_RECV_NONLINEAR_HEADER,
	OMX_COUNTER_EXP_EVENTQ_FULL,
	OMX_COUNTER_UNEXP_EVENTQ_FULL,
	OMX_COUNTER_SEND_NOMEM_SKB,
	OMX_COUNTER_SEND_NOMEM_MEDIUM_DEFEVENT,
	OMX_COUNTER_MEDIUMSQ_FRAG_SEND_LINEAR,
	OMX_COUNTER_PULL_NONFIRST_BLOCK_DONE_EARLY,
	OMX_COUNTER_PULL_REQUEST_NOTONLYFIRST_BLOCKS,
	OMX_COUNTER_PULL_TIMEOUT_HANDLER_FIRST_BLOCK,
	OMX_COUNTER_PULL_TIMEOUT_HANDLER_NONFIRST_BLOCK,
	OMX_COUNTER_PULL_TIMEOUT_ABORT,
	OMX_COUNTER_PULL_REPLY_SEND_LINEAR,
	OMX_COUNTER_PULL_REPLY_FILL_FAILED,

	OMX_COUNTER_DROP_BAD_HEADER_DATALEN,
	OMX_COUNTER_DROP_BAD_DATALEN,
	OMX_COUNTER_DROP_BAD_SKBLEN,
	OMX_COUNTER_DROP_BAD_PEER_ADDR,
	OMX_COUNTER_DROP_BAD_PEER_INDEX,
	OMX_COUNTER_DROP_BAD_ENDPOINT,
	OMX_COUNTER_DROP_BAD_SESSION,
	OMX_COUNTER_DROP_PULL_BAD_REPLIES,
	OMX_COUNTER_DROP_PULL_BAD_REGION,
	OMX_COUNTER_DROP_PULL_BAD_OFFSET_LENGTH,
	OMX_COUNTER_DROP_PULL_REPLY_BAD_MAGIC_ENDPOINT,
	OMX_COUNTER_DROP_PULL_REPLY_BAD_WIRE_HANDLE,
	OMX_COUNTER_DROP_PULL_REPLY_BAD_SEQNUM_WRAPAROUND,
	OMX_COUNTER_DROP_PULL_REPLY_BAD_SEQNUM,
	OMX_COUNTER_DROP_PULL_REPLY_DUPLICATE,
	OMX_COUNTER_DROP_NACK_MCP_BAD_MAGIC_ENDPOINT,
	OMX_COUNTER_DROP_NACK_MCP_BAD_WIRE_HANDLE,
	OMX_COUNTER_DROP_HOST_REPLY_BAD_MAGIC,
	OMX_COUNTER_DROP_RAW_QUEUE_FULL,
	OMX_COUNTER_DROP_RAW_TOO_LARGE,
	OMX_COUNTER_DROP_NOSYS_TYPE,
	OMX_COUNTER_DROP_INVALID_TYPE,
	OMX_COUNTER_DROP_UNKNOWN_TYPE,

	OMX_COUNTER_SHARED_TINY,
	OMX_COUNTER_SHARED_SMALL,
	OMX_COUNTER_SHARED_MEDIUMSQ_FRAG,
	OMX_COUNTER_SHARED_MEDIUMVA,
	OMX_COUNTER_SHARED_RNDV,
	OMX_COUNTER_SHARED_NOTIFY,
	OMX_COUNTER_SHARED_CONNECT,
	OMX_COUNTER_SHARED_TRUC,
	OMX_COUNTER_SHARED_PULL,

	OMX_COUNTER_SHARED_DMA_MEDIUM_FRAG,
	OMX_COUNTER_SHARED_DMA_LARGE,
	OMX_COUNTER_SHARED_DMA_PARTIAL_LARGE,

	OMX_COUNTER_INDEX_MAX,
};

static inline const char *
omx_strcounter(enum omx_counter_index index)
{
	switch (index) {
	case OMX_COUNTER_SEND_TINY:
		return "Send Tiny";
	case OMX_COUNTER_SEND_SMALL:
		return "Send Small";
	case OMX_COUNTER_SEND_MEDIUMSQ_FRAG:
		return "Send MediumSQ Frag";
	case OMX_COUNTER_SEND_MEDIUMVA_FRAG:
		return "Send MediumVA Frag";
	case OMX_COUNTER_SEND_RNDV:
		return "Send Rndv";
	case OMX_COUNTER_SEND_NOTIFY:
		return "Send Notify";
	case OMX_COUNTER_SEND_CONNECT:
		return "Send Connect";
	case OMX_COUNTER_SEND_TRUC:
		return "Send Truc";
	case OMX_COUNTER_SEND_NACK_LIB:
		return "Send Nack Lib";
	case OMX_COUNTER_SEND_NACK_MCP:
		return "Send Nack MCP";
	case OMX_COUNTER_SEND_PULL_REQ:
		return "Send Pull Request";
	case OMX_COUNTER_SEND_PULL_REPLY:
		return "Send Pull Reply";
	case OMX_COUNTER_SEND_RAW:
		return "Send Raw";
	case OMX_COUNTER_SEND_HOST_QUERY:
		return "Send Host Query";
	case OMX_COUNTER_SEND_HOST_REPLY:
		return "Send Host Reply";
	case OMX_COUNTER_RECV_TINY:
		return "Recv Tiny";
	case OMX_COUNTER_RECV_SMALL:
		return "Recv Small";
	case OMX_COUNTER_RECV_MEDIUM_FRAG:
		return "Recv Medium Frag";
	case OMX_COUNTER_RECV_RNDV:
		return "Recv Rndv";
	case OMX_COUNTER_RECV_NOTIFY:
		return "Recv Notify";
	case OMX_COUNTER_RECV_CONNECT:
		return "Recv Connect";
	case OMX_COUNTER_RECV_TRUC:
		return "Recv Truc";
	case OMX_COUNTER_RECV_NACK_LIB:
		return "Recv Nack Lib";
	case OMX_COUNTER_RECV_NACK_MCP:
		return "Recv Nack MCP";
	case OMX_COUNTER_RECV_PULL_REQ:
		return "Recv Pull Request";
	case OMX_COUNTER_RECV_PULL_REPLY:
		return "Recv Pull Reply";
	case OMX_COUNTER_RECV_RAW:
		return "Recv Raw";
	case OMX_COUNTER_RECV_HOST_QUERY:
		return "Recv Host Query";
	case OMX_COUNTER_RECV_HOST_REPLY:
		return "Recv Host Reply";
	case OMX_COUNTER_DMARECV_MEDIUM_FRAG:
		return "DMA Recv Medium Frag";
	case OMX_COUNTER_DMARECV_PARTIAL_MEDIUM_FRAG:
		return "DMA Recv Medium Frag Only Partial";
	case OMX_COUNTER_DMARECV_PULL_REPLY:
		return "DMA Recv Pull Reply";
	case OMX_COUNTER_DMARECV_PARTIAL_PULL_REPLY:
		return "DMA Recv Pull Reply Only Partial";
	case OMX_COUNTER_DMARECV_PULL_REPLY_WAIT_DEFERRED:
		return "DMA Recv Pull Reply with Deferred Wait";
	case OMX_COUNTER_RECV_NONLINEAR_HEADER:
		return "Recv Open-MX Header as Non-Linear";
	case OMX_COUNTER_EXP_EVENTQ_FULL:
		return "Expected Event Queue Full";
	case OMX_COUNTER_UNEXP_EVENTQ_FULL:
		return "Unexpected Event Queue Full";
	case OMX_COUNTER_SEND_NOMEM_SKB:
		return "Send Skbuff Alloc Failed";
	case OMX_COUNTER_SEND_NOMEM_MEDIUM_DEFEVENT:
		return "Send Medium Deferred Event Alloc Failed";
	case OMX_COUNTER_MEDIUMSQ_FRAG_SEND_LINEAR:
		return "MediumSQ Frag Sent as Linear";
	case OMX_COUNTER_PULL_NONFIRST_BLOCK_DONE_EARLY:
		return "Pull Non-First Block Done before First One";
	case OMX_COUNTER_PULL_REQUEST_NOTONLYFIRST_BLOCKS:
		return "Pull Request for Not Only the First Block at Once";
	case OMX_COUNTER_PULL_TIMEOUT_HANDLER_FIRST_BLOCK:
		return "Pull Timeout Handler Requests First Block";
	case OMX_COUNTER_PULL_TIMEOUT_HANDLER_NONFIRST_BLOCK:
		return "Pull Timeout Handler Requests Non-First Block";
	case OMX_COUNTER_PULL_TIMEOUT_ABORT:
		return "Pull Timeout Abort";
	case OMX_COUNTER_PULL_REPLY_SEND_LINEAR:
		return "Pull Reply Sent as Linear";
	case OMX_COUNTER_PULL_REPLY_FILL_FAILED:
		return "Pull Reply Recv Fill Pages Failed";
	case OMX_COUNTER_DROP_BAD_HEADER_DATALEN:
	       	return "Drop Bad Data Length for Headers";
	case OMX_COUNTER_DROP_BAD_DATALEN:
		return "Drop Bad Data Length";
	case OMX_COUNTER_DROP_BAD_SKBLEN:
		return "Drop Bad Skbuff Length";
	case OMX_COUNTER_DROP_BAD_PEER_ADDR:
		return "Drop Bad Peer Addr";
	case OMX_COUNTER_DROP_BAD_PEER_INDEX:
		return "Drop Bad Peer Index";
	case OMX_COUNTER_DROP_BAD_ENDPOINT:
		return "Drop Bad Endpoint";
	case OMX_COUNTER_DROP_BAD_SESSION:
		return "Drop Bad Session";
	case OMX_COUNTER_DROP_PULL_BAD_REPLIES:
		return "Drop Pull Bad Number of Replies";
	case OMX_COUNTER_DROP_PULL_BAD_REGION:
		return "Drop Pull Bad Region";
	case OMX_COUNTER_DROP_PULL_BAD_OFFSET_LENGTH:
		return "Drop Pull Bad Offset or Length";
	case OMX_COUNTER_DROP_PULL_REPLY_BAD_MAGIC_ENDPOINT:
		return "Drop Pull Reply Bad Endpoint in Magic";
	case OMX_COUNTER_DROP_PULL_REPLY_BAD_WIRE_HANDLE:
		return "Drop Pull Reply Bad Wire Handle";
	case OMX_COUNTER_DROP_PULL_REPLY_BAD_SEQNUM_WRAPAROUND:
		return "Drop Pull Reply Bad Frame SeqNum WrapAround";
	case OMX_COUNTER_DROP_PULL_REPLY_BAD_SEQNUM:
		return "Drop Pull Reply Bad Frame SeqNum";
	case OMX_COUNTER_DROP_PULL_REPLY_DUPLICATE:
		return "Drop Pull Reply Duplicate";
	case OMX_COUNTER_DROP_NACK_MCP_BAD_MAGIC_ENDPOINT:
		return "Drop Nack MCP Bad Endpoint in Magic";
	case OMX_COUNTER_DROP_NACK_MCP_BAD_WIRE_HANDLE:
		return "Drop Nack MCP Bad Wire Handle";
	case OMX_COUNTER_DROP_HOST_REPLY_BAD_MAGIC:
		return "Drop Host Reply with Bad Magic";
	case OMX_COUNTER_DROP_RAW_QUEUE_FULL:
		return "Drop Raw Queue Full";
	case OMX_COUNTER_DROP_RAW_TOO_LARGE:
		return "Drop Raw Packet Too Large";
	case OMX_COUNTER_DROP_NOSYS_TYPE:
		return "Drop Not Implemented Packet Type";
	case OMX_COUNTER_DROP_INVALID_TYPE:
		return "Drop Invalid Packet Type";
	case OMX_COUNTER_DROP_UNKNOWN_TYPE:
		return "Drop Unknown Packet Type";
	case OMX_COUNTER_SHARED_TINY:
		return "Shared Tiny";
	case OMX_COUNTER_SHARED_SMALL:
		return "Shared Small";
	case OMX_COUNTER_SHARED_MEDIUMSQ_FRAG:
		return "Shared MediumSQ Frag";
	case OMX_COUNTER_SHARED_MEDIUMVA:
		return "Shared MediumVA";
	case OMX_COUNTER_SHARED_RNDV:
		return "Shared Rndv";
	case OMX_COUNTER_SHARED_NOTIFY:
		return "Shared Notify";
	case OMX_COUNTER_SHARED_CONNECT:
		return "Shared Connect";
	case OMX_COUNTER_SHARED_TRUC:
		return "Shared Truc";
	case OMX_COUNTER_SHARED_PULL:
		return "Shared Pull";
	case OMX_COUNTER_SHARED_DMA_MEDIUM_FRAG:
		return "DMA Shared Medium Frag";
	case OMX_COUNTER_SHARED_DMA_LARGE:
		return "DMA Shared Large";
	case OMX_COUNTER_SHARED_DMA_PARTIAL_LARGE:
		return "DMA Shared Large only Partial";
	default:
		return "** Unknown **";
	}
}

#endif /* __omx_io_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
