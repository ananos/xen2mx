#ifndef __mpoe_lib_h__
#define __mpoe_lib_h__

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "mpoe_io.h"
#include "mpoe_list.h"
#include "mpoe__valgrind.h"

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
  int endpoint_index, board_index;
  char board_name[MPOE_IF_NAMESIZE];
  uint64_t board_addr;
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
  MPOE_ALREADY_INITIALIZED,
  MPOE_NOT_INITIALIZED,
  MPOE_NO_DEVICE,
  MPOE_ACCESS_DENIED,
  MPOE_NO_RESOURCES,
  MPOE_NO_SYSTEM_RESOURCES,
  MPOE_INVALID_PARAMETER,
  MPOE_NOT_IMPLEMENTED,
};
typedef enum mpoe_return mpoe_return_t;

enum mpoe_status_code {
  MPOE_STATUS_SUCCESS=0,
  MPOE_STATUS_FAILED,
};
typedef enum mpoe_status_code mpoe_status_code_t;

struct mpoe_status {
  enum mpoe_status_code code;
  uint64_t board_addr;
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

#define MPOE_API 0x0

mpoe_return_t
mpoe__init_api(int api);

static inline mpoe_return_t mpoe_init(void) { return mpoe__init_api(MPOE_API); }

const char *
mpoe_strerror(mpoe_return_t ret);

const char *
mpoe_strstatus(mpoe_status_code_t code);

mpoe_return_t
mpoe_board_number_to_nic_id(uint32_t board_number,
			    uint64_t *nic_id);

mpoe_return_t
mpoe_nic_id_to_board_number(uint64_t nic_id,
			    uint32_t *board_number);

mpoe_return_t
mpoe_open_endpoint(uint32_t board_index, uint32_t index,
		   struct mpoe_endpoint **epp);

mpoe_return_t
mpoe_close_endpoint(struct mpoe_endpoint *ep);

mpoe_return_t
mpoe_isend(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info,
	   uint64_t dest_addr, uint32_t dest_endpoint,
	   void * context, union mpoe_request ** request);

mpoe_return_t
mpoe_irecv(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, union mpoe_request **requestp);

mpoe_return_t
mpoe_test(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status, uint32_t * result);

mpoe_return_t
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
  /* return the board name of an endpoint or index (given as uint8_t) */
  MPOE_INFO_BOARD_NAME,
  /* return the board addr of an endpoint or index (given as uint8_t) */
  MPOE_INFO_BOARD_ADDR,
  /* return the board number of an endpoint or name */
  MPOE_INFO_BOARD_INDEX_BY_NAME,
  /* return the board number of an endpoint or addr */
  MPOE_INFO_BOARD_INDEX_BY_ADDR,
};

mpoe_return_t
mpoe_get_info(struct mpoe_endpoint * ep, enum mpoe_info_key key,
	      const void * in_val, uint32_t in_len,
	      void * out_val, uint32_t out_len);

#define MPOE_BOARD_ADDR_STRLEN 18

static inline int
mpoe_board_addr_sprintf(char * buffer, uint64_t addr)
{
	return sprintf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		       (uint8_t)(addr >> 40),
		       (uint8_t)(addr >> 32),
		       (uint8_t)(addr >> 24),
		       (uint8_t)(addr >> 16),
		       (uint8_t)(addr >> 8),
		       (uint8_t)(addr >> 0));
}

static inline int
mpoe_board_addr_sscanf(char * buffer, uint64_t * addr)
{
	uint8_t bytes[6];
	int err;

        err = sscanf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		     &bytes[0], &bytes[1], &bytes[2],
		     &bytes[3], &bytes[4], &bytes[5]);

	if (err == 6)
		*addr = (((uint64_t) bytes[0]) << 40)
		      + (((uint64_t) bytes[1]) << 32)
		      + (((uint64_t) bytes[2]) << 24)
		      + (((uint64_t) bytes[3]) << 16)
		      + (((uint64_t) bytes[4]) << 8)
		      + (((uint64_t) bytes[5]) << 0);

	return err;
}

#endif /* __mpoe_lib_h__ */
