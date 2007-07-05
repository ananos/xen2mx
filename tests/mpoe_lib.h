#ifndef __mpoe_lib_h__
#define __mpoe_lib_h__

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "mpoe_io.h"
#include "mpoe_list.h"

/* FIXME: assertion to check MPOE_IF_NAMESIZE == IF_NAMESIZE */

struct mpoe_endpoint {
  int fd;
  void * recvq, * sendq, * eventq;
  void * next_event;
  struct list_head sent_req_q;
  struct list_head unexp_req_q;
  struct list_head recv_req_q;
  struct list_head multifraq_medium_recv_req_q;
  struct list_head done_req_q;
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
    uint32_t lib_cookie;
    union {
      struct {
	uint32_t frames_pending_nr;
      } medium;
    } type;
  } send;

  struct {
    struct mpoe__generic_request generic;
    void * buffer;
    unsigned long length;
    union {
      struct {
	uint32_t frames_received_mask;
	uint32_t accumulated_length;
      } medium;
    } type;
  } recv;
};

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
