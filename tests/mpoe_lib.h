#ifndef __mpoe_lib_h__
#define __mpoe_lib_h__

#include <string.h>
#include <stdint.h>

#include "mpoe_io.h"

/* FIXME: assertion to check MPOE_IF_NAMESIZE == IF_NAMESIZE */

struct mpoe_endpoint {
  int fd;
  void * recvq, * sendq, * eventq;
  void * next_event;
  union mpoe_request * sent_req_q;
  union mpoe_request * unexp_req_q;
  union mpoe_request * recv_req_q;
  union mpoe_request * done_req_q;
};

enum mpoe__request_type {
  MPOE_REQUEST_TYPE_NONE=0,
  MPOE_REQUEST_TYPE_SEND_TINY,
  MPOE_REQUEST_TYPE_SEND_MEDIUM,
  MPOE_REQUEST_TYPE_RECV,
};

enum mpoe__request_state {
  MPOE_REQUEST_STATE_PENDING=0,
  MPOE_REQUEST_STATE_DONE,
};

enum mpoe_status_code {
  MPOE_STATUS_SUCCESS=0,
  MPOE_STATUS_FAILED,
};

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
  union mpoe_request * next;
  enum mpoe__request_type type;
  enum mpoe__request_state state;
  struct mpoe_status status;
};

union mpoe_request {
  struct mpoe__generic_request generic;
  struct {
    struct mpoe__generic_request generic;
    uint32_t lib_cookie;
  } send;
  struct {
    struct mpoe__generic_request generic;
    void * buffer;
    unsigned long length;
  } recv;
};

extern int
mpoe_open_endpoint(uint32_t board_index, uint32_t index,
		   struct mpoe_endpoint **epp);

extern int
mpoe_close_endpoint(struct mpoe_endpoint *ep);

extern int
mpoe_isend(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info,
	   struct mpoe_mac_addr * dest_addr, uint32_t dest_endpoint,
	   void * context, union mpoe_request ** request);

extern int
mpoe_irecv(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, union mpoe_request **requestp);

extern int
mpoe_wait(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status);

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
