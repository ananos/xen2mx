#ifndef __mpoe_wire_h__
#define __mpoe_wire_h__

/******************************
 * Packet definition
 */

#define ETH_P_MPOE 0x86DF

enum mpoe_pkt_type {
	MPOE_PKT_NONE=0,
	MPOE_PKT_TINY,
	MPOE_PKT_SMALL,
	MPOE_PKT_MEDIUM,
	MPOE_PKT_RENDEZ_VOUS,
	MPOE_PKT_PULL,
	MPOE_PKT_PULL_REPLY,
};

struct mpoe_pkt_head {
	struct ethhdr eth;
	uint16_t sender_peer_index; /* FIXME: unused */
	/* 16 */
};

struct mpoe_pkt_msg {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint16_t length;
	uint16_t pad2;
	uint16_t lib_seqnum; /* FIXME: unused ? */
	uint16_t lib_piggyack; /* FIXME: unused ? */
	uint32_t match_a;
	uint32_t match_b;
	uint32_t session; /* FIXME: unused ? */
	/* 24 */
};

struct mpoe_pkt_medium_frag {
	struct mpoe_pkt_msg msg;
	uint16_t length; /* FIXME: unused ? */
	uint8_t seqnum; /* FIXME: unused ? */
	uint8_t pipeline; /* FIXME: unused ? */
	uint32_t pad;
};

struct mpoe_pkt_pull_request {
	uint8_t ptype;
	uint8_t dst_endpoint;
	uint8_t src_endpoint;
	uint8_t src_generation; /* FIXME: unused ? */
	uint32_t session; /* FIXME: unused ? */
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t puller_rdma_id;
	uint32_t puller_offset; /* FIXME: 64bits ? */
	uint32_t pulled_rdma_id;
	uint32_t pulled_offset; /* FIXME: 64bits ? */
	uint32_t src_pull_handle; /* sender's handle id */
	uint32_t src_magic; /* sender's endpoint magic */
#if 0
	uint32_t total_length; /* full message total length */
	uint8_t rdmawin_id; /* target window id */
	uint8_t rdmawin_seqnum; /* target window seqnum */
	uint16_t rdma_offset; /* offset in target window first page */
	uint16_t offset; /* sender's first page offset */
	uint16_t pull_length; /* this pull length (pull_reply * pagesize) - target offset */
	uint32_t index; /* pull interation index (page_nr/page_per_pull) */
#endif
};

struct mpoe_pkt_pull_reply {
	uint8_t ptype;
	uint8_t pad[3];
	uint32_t length; /* FIXME: 64bits ? */
	uint32_t puller_rdma_id;
	uint32_t puller_offset; /* FIXME: 64bits ? */
	uint32_t dst_pull_handle; /* sender's handle id */
	uint32_t dst_magic; /* sender's endpoint magic */
#if 0
	uint8_t frame_seqnum; /* sender's pull index + page number in this frame */
	uint16_t frame_length; /* pagesize - frame_offset */
	uint32_t msg_offset; /* index * pagesize - target_offset + sender_offset */
#endif
};

struct mpoe_hdr {
	struct mpoe_pkt_head head;
	/* 32 */
	union {
		struct mpoe_pkt_msg generic;
		struct mpoe_pkt_msg tiny;
		struct mpoe_pkt_msg small;
		struct mpoe_pkt_medium_frag medium;
		struct mpoe_pkt_pull_request pull;
		struct mpoe_pkt_pull_reply pull_reply;
	} body;
};

#endif /* __mpoe_wire_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
