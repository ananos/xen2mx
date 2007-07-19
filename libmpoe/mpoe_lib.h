#ifndef __mpoe_lib_h__
#define __mpoe_lib_h__

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "mpoe_io.h"
#include "mpoe_list.h"

/********
 * Types
 */

struct mpoe_sendq_map {
  int first_free;
  int nr_free;
  struct mpoe_sendq_entry {
    int next_free;
    void * user;
  } * array;
};

typedef uint16_t mpoe_seqnum_t; /* FIXME: assert same size on the wire */

struct mpoe_partner {
  /* list of request matched but not entirely received */
  struct list_head partialq;

  /* seqnum of the next send */
  mpoe_seqnum_t next_send_seq;

  /* seqnum of the next entire message to match
   * used to know to accumulate/match/defer a fragment
   */
  mpoe_seqnum_t next_match_recv_seq;

  /* seqnum of the next fragment to recv
   * next_frag_recv_seq < next_match_recv_seq in case of partially received medium
   * used to ack back to the partner
   * (all seqnum < next_frag_recv_seq have been entirely received)
   */
  mpoe_seqnum_t next_frag_recv_seq;


  /*
   * when matching, increase recv_seq
   * when event, compare message seqnum with next_match_recv_seq:
   * - if == , matching
   * - if < , find partial receive in partner's queue
   * - if < , queue as a early fragment
   *
   * when completing an event, recompute next_frag_recv_seq
   * - if partial receive (ordered), use its seqnum
   * - if no partial receive, use next_match_recv_seq
   * if changing next_frag_recv_seq, ack all the previous seqnums
   */

};

struct mpoe_endpoint {
  int fd;
  void * recvq, * sendq, * eventq;
  void * next_event;
  struct list_head sent_req_q;
  struct list_head unexp_req_q;
  struct list_head recv_req_q;
  struct list_head multifraq_medium_recv_req_q;
  struct list_head done_req_q;
  struct mpoe_sendq_map sendq_map;
  struct mpoe_partner partner;
};

enum mpoe__request_type {
  MPOE_REQUEST_TYPE_NONE=0,
  MPOE_REQUEST_TYPE_SEND_TINY,
  MPOE_REQUEST_TYPE_SEND_SMALL,
  MPOE_REQUEST_TYPE_SEND_MEDIUM,
  MPOE_REQUEST_TYPE_RECV,
};

enum mpoe__request_state {
  MPOE_REQUEST_STATE_PENDING=0,
  MPOE_REQUEST_STATE_DONE,
};

enum mpoe_return {
  MPOE_SUCCESS=0,
  MPOE_BAD_ERROR,
  MPOE_NO_DEVICE,
  MPOE_ACCESS_DENIED,
  MPOE_NO_RESOURCES,
  MPOE_NO_SYSTEM_RESOURCES,
  MPOE_INVALID_PARAMETER,
};
typedef enum mpoe_return mpoe_return_t;

enum mpoe_status_code {
  MPOE_STATUS_SUCCESS=0,
  MPOE_STATUS_FAILED,
};
typedef enum mpoe_status_code mpoe_status_code_t;

struct mpoe_status {
  enum mpoe_status_code code;
  struct mpoe_mac_addr mac;
  uint32_t ep;
  unsigned long msg_length;
  unsigned long xfer_length;
  uint64_t match_info;
  void *context;
};

struct mpoe__generic_request {
  struct list_head queue_elt;
  enum mpoe__request_type type;
  enum mpoe__request_state state;
  struct mpoe_status status;
};

union mpoe_request {
  struct mpoe__generic_request generic;

  struct {
    struct mpoe__generic_request generic;
    uint16_t seqnum;
    union {
      struct {
	uint32_t frags_pending_nr;
      } medium;
    } type;
  } send;

  struct {
    struct mpoe__generic_request generic;
    void * buffer;
    unsigned long length;
    union {
      struct {
	uint32_t frags_received_mask;
	uint32_t accumulated_length;
      } medium;
    } type;
  } recv;
};

/************
 * Functions
 */

const char *
mpoe_strerror(mpoe_return_t ret);

const char *
mpoe_strstatus(mpoe_status_code_t code);

extern mpoe_return_t
mpoe_open_endpoint(uint32_t board_index, uint32_t index,
		   struct mpoe_endpoint **epp);

extern mpoe_return_t
mpoe_close_endpoint(struct mpoe_endpoint *ep);

extern mpoe_return_t
mpoe_isend(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info,
	   struct mpoe_mac_addr * dest_addr, uint32_t dest_endpoint,
	   void * context, union mpoe_request ** request);

extern mpoe_return_t
mpoe_irecv(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, union mpoe_request **requestp);

mpoe_return_t
mpoe_test(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status, uint32_t * result);

extern mpoe_return_t
mpoe_wait(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status, uint32_t * result);

mpoe_return_t
mpoe_ipeek(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	   uint32_t *result);

mpoe_return_t
mpoe_peek(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  uint32_t *result);

enum mpoe_info_key {
  /* return the maximum number of boards */
  MPOE_INFO_BOARD_MAX,
  /* return the maximum number of endpoints per board */
  MPOE_INFO_ENDPOINT_MAX,
  /* return the current number of boards */
  MPOE_INFO_BOARD_COUNT,
  /* return the board name of an endpoint or index */
  MPOE_INFO_BOARD_NAME,
  /* return the board addr of an endpoint or index */
  MPOE_INFO_BOARD_ADDR,
  /* return the board number of an endpoint or name or addr */
  MPOE_INFO_BOARD_INDEX,
};

mpoe_return_t
mpoe_get_info(struct mpoe_endpoint * ep, enum mpoe_info_key key,
	      const void * in_val, uint32_t in_len,
	      void * out_val, uint32_t out_len);

static inline void
mpoe_mac_addr_copy(struct mpoe_mac_addr * dst,
		   struct mpoe_mac_addr * src)
{
	memcpy(dst, src, sizeof(struct mpoe_mac_addr));
}

static inline void
mpoe_mac_addr_set_bcast(struct mpoe_mac_addr * addr)
{
	memset(addr, 0xff, sizeof (struct mpoe_mac_addr));
}

#define MPOE_MAC_ADDR_STRLEN 18

static inline int
mpoe_mac_addr_sprintf(char * buffer, struct mpoe_mac_addr * addr)
{
	return sprintf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		       addr->hex[0], addr->hex[1], addr->hex[2],
		       addr->hex[3], addr->hex[4], addr->hex[5]);
}

static inline int
mpoe_mac_addr_sscanf(char * buffer, struct mpoe_mac_addr * addr)
{
	return sscanf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		      &addr->hex[0], &addr->hex[1], &addr->hex[2],
		      &addr->hex[3], &addr->hex[4], &addr->hex[5]);
}

#endif /* __mpoe_lib_h__ */
