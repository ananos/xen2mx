#ifndef __mpoe_io_h__
#define __mpoe_io_h__

#ifndef __KERNEL__
#include <stdint.h>
#endif

/************************
 * Common parameters or IOCTL subtypes
 */

#define MPOE_SENDQ_SIZE		(4*1024*1024)
#define MPOE_SENDQ_OFFSET	0
#define MPOE_RECVQ_SIZE		(4*1024*1024)
#define MPOE_RECVQ_OFFSET	4096
#define MPOE_EVENTQ_SIZE	(64*1024)
#define MPOE_EVENTQ_OFFSET	(2*4096)

#define MPOE_TINY_MAX           48 /* FIXME: check that it fills in the data field in the request and event below */

#define MPOE_USER_REGION_MAX		255
typedef uint8_t mpoe_user_region_id_t;

#define MPOE_IF_NAMESIZE	16

struct mpoe_cmd_region_segment {
	uint64_t vaddr;
	uint32_t len;
	uint32_t pad;
};

/************************
 * IOCTL commands
 */

#define MPOE_CMD_GET_BOARD_COUNT	0x01
#define MPOE_CMD_GET_BOARD_ID		0x02
#define MPOE_CMD_OPEN_ENDPOINT		0x81
#define MPOE_CMD_CLOSE_ENDPOINT		0x82
#define MPOE_CMD_SEND_TINY		0x83
#define MPOE_CMD_SEND_MEDIUM		0x84
#define MPOE_CMD_SEND_RENDEZ_VOUS	0x85
#define MPOE_CMD_SEND_PULL		0x86
#define MPOE_CMD_REGISTER_REGION	0x87
#define MPOE_CMD_DEREGISTER_REGION	0x88

static inline const char *
mpoe_strcmd(unsigned int cmd)
{
	switch (cmd) {
	case MPOE_CMD_GET_BOARD_COUNT:
		return "Get Board Count";
	case MPOE_CMD_GET_BOARD_ID:
		return "Get Board ID";
	case MPOE_CMD_OPEN_ENDPOINT:
		return "Open Endpoint";
	case MPOE_CMD_CLOSE_ENDPOINT:
		return "Close Endpoint";
	case MPOE_CMD_SEND_TINY:
		return "Send Tiny";
	case MPOE_CMD_SEND_MEDIUM:
		return "Send Medium";
	case MPOE_CMD_SEND_RENDEZ_VOUS:
		return "Send Rendez-vous";
	case MPOE_CMD_SEND_PULL:
		return "Send Pull";
	case MPOE_CMD_REGISTER_REGION:
		return "Register Region";
	case MPOE_CMD_DEREGISTER_REGION:
		return "Deregister Region";
	default:
		return "** Unknown **";
	}
}

/************************
 * IOCTL parameter types
 */

struct mpoe_cmd_get_board_id {
	uint8_t board_index;
	uint64_t board_addr;
	char board_name[MPOE_IF_NAMESIZE];
};

struct mpoe_cmd_open_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;
};

struct mpoe_cmd_send_tiny {
	struct mpoe_cmd_send_tiny_hdr {
		uint8_t dest_mac[6];
		uint8_t dest_endpoint;
		uint8_t length;
		uint64_t match_info;
		/* 16 */
	} hdr;
	char data[64-sizeof(struct mpoe_cmd_send_tiny_hdr)];
	/* 64 */
};

struct mpoe_cmd_send_pull_hdr {
	uint8_t dest_mac[6];
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
	uint32_t pad2[9];
	/* 64 */
};

struct mpoe_cmd_send_medium_hdr {
	uint8_t dest_mac[6];
	uint8_t dest_endpoint;
	uint8_t sendq_page_offset;
	/* 8 */
	uint32_t length;
	uint32_t offset;
	/* 16 */
	uint64_t match_info;
	/* 24 */
	uint64_t pad3[5];
	/* 64 */
};

struct mpoe_cmd_register_region {
	uint32_t nr_segments;
	uint32_t id;
	uint32_t seqnum;
	uint32_t pad;
	uint64_t memory_context;
	uint64_t segments;
};

struct mpoe_cmd_deregister_region {
	uint32_t id;
};

/************************
 * Event types
 */

#define MPOE_EVT_NONE		0x00
#define MPOE_EVT_RECV_TINY	0x01
#define MPOE_EVT_RECV_MEDIUM	0x02

static inline const char *
mpoe_strevt(unsigned int type)
{
	switch (type) {
	case MPOE_EVT_NONE:
		return "None";
	case MPOE_EVT_RECV_TINY:
		return "Receive Tiny";
	case MPOE_EVT_RECV_MEDIUM:
		return "Receive Medium Fragment";
	default:
		return "** Unknown **";
	}
}

/************************
 * Event parameter types
 */

union mpoe_evt {
	/* generic event */
	struct mpoe_evt_generic {
		char pad[63];
		uint8_t type;
		/* 64 */
	} generic;

	/* recv tiny */
	struct mpoe_evt_recv_tiny {
		uint8_t src_mac[6];
		uint8_t src_endpoint;
		uint8_t length;
		uint64_t match_info;
		/* 16 */
		char data[47];
		uint8_t type;
		/* 64 */
	} tiny;

	/* recv medium */
	struct mpoe_evt_recv_medium {
		uint8_t src_mac[6];
		uint8_t src_endpoint;
		uint8_t pad1;
		/* 8 */
		uint32_t length;
		uint32_t offset;
		/* 16 */
		uint64_t match_info;
		/* 24 */
		char data[39];
		uint8_t type;
		/* 64 */
	} medium;
};

#endif /* __mpoe_io_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
