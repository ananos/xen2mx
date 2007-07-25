#ifndef __omx_lib_h__
#define __omx_lib_h__

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "openmx.h"
#include "omx_io.h"
#include "omx_list.h"
#include "omx__valgrind.h"

/*****************
 * Various macros
 */

#define OMX_DEVNAME "/dev/openmx"
/* FIXME: envvar to configure? */

#define OMX_MEDIUM_FRAG_PIPELINE_BASE 10  /* pipeline is encoded -10 on the wire */
#define OMX_MEDIUM_FRAG_PIPELINE 2 /* always send 4k pages (1<<(10+2)) */
#define OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT (OMX_MEDIUM_FRAG_PIPELINE_BASE+OMX_MEDIUM_FRAG_PIPELINE)
#define OMX_MEDIUM_FRAG_LENGTH_MAX (1<<OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)
#define OMX_MEDIUM_FRAGS_NR(len) ((len+OMX_MEDIUM_FRAG_LENGTH_MAX-1)>>OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)

/*****************
 * Internal types
 */

struct omx__sendq_map {
  int first_free;
  int nr_free;
  struct omx__sendq_entry {
    int next_free;
    void * user;
  } * array;
};

typedef uint16_t omx__seqnum_t; /* FIXME: assert same size on the wire */

struct omx__partner {
  uint64_t board_addr;
  uint16_t peer_index;
  uint8_t endpoint_index;

  /* list of request matched but not entirely received */
  struct list_head partialq;

  /* seqnum of the next send */
  omx__seqnum_t next_send_seq;

  /* seqnum of the next entire message to match
   * used to know to accumulate/match/defer a fragment
   */
  omx__seqnum_t next_match_recv_seq;

  /* seqnum of the next fragment to recv
   * next_frag_recv_seq < next_match_recv_seq in case of partially received medium
   * used to ack back to the partner
   * (all seqnum < next_frag_recv_seq have been entirely received)
   */
  omx__seqnum_t next_frag_recv_seq;


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
  struct omx__sendq_map sendq_map;
  struct omx__partner ** partners;
  struct omx__partner * myself;
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
    omx__seqnum_t seqnum;
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

struct omx__globals {
  int initialized;
  int control_fd;
  uint32_t board_max;
  uint32_t endpoint_max;
  uint32_t peer_max;
};

extern struct omx__globals omx__globals;

/*********************
 * Internal functions
 */

static inline struct omx__partner *
omx__partner_from_addr(omx_endpoint_addr_t * addr)
{
  return *(struct omx__partner **) addr;
}

static inline void
omx__partner_to_addr(struct omx__partner * partner, omx_endpoint_addr_t * addr)
{
  *(struct omx__partner **) addr = partner;
  OMX_VALGRIND_MEMORY_MAKE_READABLE(addr, sizeof(*addr));
}

static inline int
omx__board_addr_sprintf(char * buffer, uint64_t addr)
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
omx__board_addr_sscanf(char * buffer, uint64_t * addr)
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

extern omx_return_t omx__errno_to_return(int error, char * caller);

extern omx_return_t omx__get_board_count(uint32_t * count);
extern omx_return_t omx__get_board_id(struct omx_endpoint * ep, uint8_t * index, char * name, uint64_t * addr);
extern omx_return_t omx__get_board_index_by_name(const char * name, uint8_t * index);

extern omx_return_t omx__peers_init(void);
extern omx_return_t omx__peers_dump(const char * format);
extern omx_return_t omx__peer_addr_to_index(uint64_t board_addr, uint16_t *index);
extern omx_return_t omx__peer_from_index(uint16_t index, uint64_t *board_addr, char **hostname);

#endif /* __omx_lib_h__ */
