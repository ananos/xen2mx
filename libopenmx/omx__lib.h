#ifndef __omx_lib_h__
#define __omx_lib_h__

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "omx_io.h"
#include "omx_list.h"
#include "omx__valgrind.h"

/********
 * Types
 */

struct omx_sendq_map {
  int first_free;
  int nr_free;
  struct omx_sendq_entry {
    int next_free;
    void * user;
  } * array;
};

typedef uint16_t omx_seqnum_t; /* FIXME: assert same size on the wire */

struct omx_partner {
  /* list of request matched but not entirely received */
  struct list_head partialq;

  /* seqnum of the next send */
  omx_seqnum_t next_send_seq;

  /* seqnum of the next entire message to match
   * used to know to accumulate/match/defer a fragment
   */
  omx_seqnum_t next_match_recv_seq;

  /* seqnum of the next fragment to recv
   * next_frag_recv_seq < next_match_recv_seq in case of partially received medium
   * used to ack back to the partner
   * (all seqnum < next_frag_recv_seq have been entirely received)
   */
  omx_seqnum_t next_frag_recv_seq;


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

struct omx_endpoint {
  int fd;
  int endpoint_index, board_index;
  char board_name[OMX_HOSTNAMELEN_MAX];
  uint64_t board_addr;
  void * recvq, * sendq, * eventq;
  void * next_event;
  struct list_head sent_req_q;
  struct list_head unexp_req_q;
  struct list_head recv_req_q;
  struct list_head multifraq_medium_recv_req_q;
  struct list_head done_req_q;
  struct omx_sendq_map sendq_map;
  struct omx_partner partner;
};

enum omx__request_type {
  OMX_REQUEST_TYPE_NONE=0,
  OMX_REQUEST_TYPE_SEND_TINY,
  OMX_REQUEST_TYPE_SEND_SMALL,
  OMX_REQUEST_TYPE_SEND_MEDIUM,
  OMX_REQUEST_TYPE_RECV,
};

enum omx__request_state {
  OMX_REQUEST_STATE_PENDING=0,
  OMX_REQUEST_STATE_DONE,
};

enum omx_return {
  OMX_SUCCESS=0,
  OMX_BAD_ERROR,
  OMX_ALREADY_INITIALIZED,
  OMX_NOT_INITIALIZED,
  OMX_NO_DEVICE,
  OMX_ACCESS_DENIED,
  OMX_NO_RESOURCES,
  OMX_NO_SYSTEM_RESOURCES,
  OMX_INVALID_PARAMETER,
  OMX_NOT_IMPLEMENTED,
};
typedef enum omx_return omx_return_t;

enum omx_status_code {
  OMX_STATUS_SUCCESS=0,
  OMX_STATUS_FAILED,
};
typedef enum omx_status_code omx_status_code_t;

struct omx_status {
  enum omx_status_code code;
  uint64_t board_addr;
  uint32_t ep;
  unsigned long msg_length;
  unsigned long xfer_length;
  uint64_t match_info;
  void *context;
};

struct omx__generic_request {
  struct list_head queue_elt;
  enum omx__request_type type;
  enum omx__request_state state;
  struct omx_status status;
};

union omx_request {
  struct omx__generic_request generic;

  struct {
    struct omx__generic_request generic;
    uint16_t seqnum;
    union {
      struct {
	uint32_t frags_pending_nr;
      } medium;
    } type;
  } send;

  struct {
    struct omx__generic_request generic;
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

#define OMX_API 0x0

omx_return_t
omx__init_api(int api);

static inline omx_return_t omx_init(void) { return omx__init_api(OMX_API); }

const char *
omx_strerror(omx_return_t ret);

const char *
omx_strstatus(omx_status_code_t code);

omx_return_t
omx_board_number_to_nic_id(uint32_t board_number,
			   uint64_t *nic_id);

omx_return_t
omx_nic_id_to_board_number(uint64_t nic_id,
			   uint32_t *board_number);

omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t index,
		  struct omx_endpoint **epp);

omx_return_t
omx_close_endpoint(struct omx_endpoint *ep);

omx_return_t
omx_isend(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  uint64_t match_info,
	  uint64_t dest_addr, uint32_t dest_endpoint,
	  void * context, union omx_request ** request);

omx_return_t
omx_irecv(struct omx_endpoint *ep,
	  void *buffer, size_t length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, union omx_request **requestp);

omx_return_t
omx_test(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_wait(struct omx_endpoint *ep, union omx_request **requestp,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_ipeek(struct omx_endpoint *ep, union omx_request **requestp,
	  uint32_t *result);

omx_return_t
omx_peek(struct omx_endpoint *ep, union omx_request **requestp,
	 uint32_t *result);

enum omx_info_key {
  /* return the maximum number of boards */
  OMX_INFO_BOARD_MAX,
  /* return the maximum number of endpoints per board */
  OMX_INFO_ENDPOINT_MAX,
  /* return the current number of boards */
  OMX_INFO_BOARD_COUNT,
  /* return the board name of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_NAME,
  /* return the board addr of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_ADDR,
  /* return the board number of an endpoint or name */
  OMX_INFO_BOARD_INDEX_BY_NAME,
  /* return the board number of an endpoint or addr */
  OMX_INFO_BOARD_INDEX_BY_ADDR,
};

omx_return_t
omx_get_info(struct omx_endpoint * ep, enum omx_info_key key,
	     const void * in_val, uint32_t in_len,
	     void * out_val, uint32_t out_len);

#define OMX_HOSTNAMELEN_MAX 80

#define OMX_BOARD_ADDR_STRLEN 18

omx_return_t
omx_hostname_to_nic_id(char *hostname,
		       uint64_t *board_addr);

omx_return_t
omx_nic_id_to_hostname(uint64_t board_addr,
		       char *hostname);

static inline int
omx_board_addr_sprintf(char * buffer, uint64_t addr)
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
omx_board_addr_sscanf(char * buffer, uint64_t * addr)
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

#endif /* __omx_lib_h__ */
