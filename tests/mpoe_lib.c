#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "mpoe_io.h"
#include "mpoe_lib.h"
#include "mpoe_internals.h"

int
mpoe_get_board_count(uint32_t * count)
{
  int ret;
  int fd;

  ret = open(MPOE_DEVNAME, O_RDWR);
  if (ret < 0) {
    perror("open");
    goto out;
  }
  fd = ret;

  ret = ioctl(fd, MPOE_CMD_GET_BOARD_COUNT, &count);
  if (ret < 0) {
    perror("get board id");
  }

  close(fd);
 out:
  return ret;
}

/* FIXME: get board id */

int
mpoe_open_endpoint(uint32_t board_index, uint32_t index,
		   struct mpoe_endpoint **epp)
{
  /* FIXME: add parameters to choose the board name? */
  struct mpoe_cmd_open_endpoint open_param;
  struct mpoe_endpoint * ep;
  void * recvq, * sendq, * eventq;
  int fd;
  int ret = 0;

  ep = malloc(sizeof(struct mpoe_endpoint));
  if (!ep) {
    perror("endpoint malloc");
    ret = -ENOMEM;
    goto out;
  }

  fd = open(MPOE_DEVNAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    ret = fd;
    goto out_with_ep;
  }

  /* ok */
  open_param.board_index = board_index;
  open_param.endpoint_index = index;
  ret = ioctl(fd, MPOE_CMD_OPEN_ENDPOINT, &open_param);
  if (ret < 0) {
    perror("attach endpoint");
    goto out_with_fd;
  }
  fprintf(stderr, "Successfully attached endpoint %d/%d\n", 0, 34);

  /* mmap */
  sendq = mmap(0, MPOE_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_SENDQ_OFFSET);
  recvq = mmap(0, MPOE_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_RECVQ_OFFSET);
  eventq = mmap(0, MPOE_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_EVENTQ_OFFSET);
  printf("sendq at %p, recvq at %p, eventq at %p\n", sendq, recvq, eventq);

  ep->fd = fd;
  ep->sendq = sendq;
  ep->recvq = recvq;
  ep->eventq = ep->next_event = eventq;

  ep->sent_req_q = NULL;
  ep->unexp_req_q = NULL;
  ep->recv_req_q = NULL;
  ep->done_req_q = NULL;

  *epp = ep;

  return 0;

 out_with_fd:
  close(fd);
 out_with_ep:
  free(ep);
 out:
  return ret;
}

static inline void
mpoe_enqueue_request(union mpoe_request **headp,
		     union mpoe_request *req)
{
  req->generic.next = NULL;
  if (!*headp) {
    *headp = req;
  } else {
    union mpoe_request * prev;
    for(prev = *headp;
	prev->generic.next != NULL;
	prev = prev->generic.next);
    prev->generic.next = req;
  }
}

static inline void
mpoe_dequeue_request(union mpoe_request **headp,
		     union mpoe_request *req)
{
  if (*headp == req) {
    *headp = req->generic.next;
  } else {
    union mpoe_request ** prev;
    for(prev = headp;
	*prev && (*prev) != req;
	prev = &(*prev)->generic.next);
    assert(*prev);
    *prev = req->generic.next;
  }
}

static inline union mpoe_request *
mpoe_find_request_by_cookie(union mpoe_request *head,
			    uint32_t cookie)
{
  union mpoe_request * req;

  for(req = head;
      req != NULL;
      req = req->generic.next)
    if (req->send.lib_cookie == cookie)
      return req;

  return NULL;
}

static int
mpoe_progress(struct mpoe_endpoint * ep)
{
  volatile union mpoe_evt * evt;

  evt = ep->next_event;
  while (evt->generic.type == MPOE_EVT_NONE) ;

  printf("received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case MPOE_EVT_RECV_TINY: {
    if (!ep->recv_req_q)
      printf("missed a tiny unexpected\n");
    else {
      struct mpoe_evt_recv_tiny * event = &((union mpoe_evt *)evt)->recv_tiny;
      union mpoe_request * req = ep->recv_req_q;
      unsigned long length;

      mpoe_dequeue_request(&ep->recv_req_q, req);
      req->generic.state = MPOE_REQUEST_STATE_DONE;

      mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
      req->generic.status.ep = event->src_endpoint;
      req->generic.status.match_info = event->match_info;

      length = event->length > req->recv.length
	? req->recv.length : event->length;
      req->generic.status.msg_length = event->length;
      req->generic.status.xfer_length = length;
      memcpy(req->recv.buffer, (void *) evt->recv_tiny.data, length);
    }
    break;
  }

  case MPOE_EVT_RECV_MEDIUM: {
    if (!ep->recv_req_q)
      printf("missed a medium unexpected\n");
    else {
      struct mpoe_evt_recv_medium * event = &((union mpoe_evt *)evt)->recv_medium;
      union mpoe_request * req = ep->recv_req_q;
      int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
      char * buffer = ep->recvq + evt_index*4096; /* FIXME: get pagesize somehow */
      unsigned long length;

      mpoe_dequeue_request(&ep->recv_req_q, req);
      req->generic.state = MPOE_REQUEST_STATE_DONE;

      mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
      req->generic.status.ep = event->src_endpoint;
      req->generic.status.match_info = event->match_info;

      length = event->length > req->recv.length
	? req->recv.length : event->length;
      req->generic.status.msg_length = event->length;
      req->generic.status.xfer_length = length;
      memcpy(req->recv.buffer, buffer, length);
    }
    break;
  }

  case MPOE_EVT_SEND_DONE: {
    uint32_t lib_cookie = evt->send_done.lib_cookie;
    union mpoe_request * req;
    printf("send done, cookie %ld\n", (unsigned long) lib_cookie);
    req = mpoe_find_request_by_cookie(ep->sent_req_q, lib_cookie);
    mpoe_dequeue_request(&ep->sent_req_q, req);
    req->generic.state = MPOE_REQUEST_STATE_DONE;
    break;
  }

  default:
    printf("unknown type\n");
    assert(0);
  }

  /* mark event as done */
  evt->generic.type = MPOE_EVT_NONE;

  /* next event */
  evt++;
  if ((void *) evt >= ep->eventq + MPOE_EVENTQ_SIZE)
    evt = ep->eventq;
  ep->next_event = (void *) evt;

  return 0;
}

int
mpoe_isend(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info,
	   struct mpoe_mac_addr *dest_addr, uint32_t dest_endpoint,
	   void *context, union mpoe_request **requestp)
{
  union mpoe_request * req;
  int ret;
  static int send_lib_cookie = 0;

  req = malloc(sizeof(union mpoe_request));
  if (!req) {
    perror("request malloc");
    return -ENOMEM;
  }

  if (length <= MPOE_TINY_MAX) {
    struct mpoe_cmd_send_tiny tiny_param;

    mpoe_mac_addr_copy(&tiny_param.hdr.dest_addr, dest_addr);
    tiny_param.hdr.dest_endpoint = dest_endpoint;
    tiny_param.hdr.match_info = match_info;
    tiny_param.hdr.length = length;
    tiny_param.hdr.lib_cookie = send_lib_cookie;
    memcpy(tiny_param.data, buffer, length);

    ret = ioctl(ep->fd, MPOE_CMD_SEND_TINY, &tiny_param);
    if (ret < 0) {
      perror("ioctl/send/tiny");
      free(req);
      return ret;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_TINY;

  } else {
    struct mpoe_cmd_send_medium_hdr medium_param;

    mpoe_mac_addr_copy(&medium_param.dest_addr, dest_addr);
    medium_param.dest_endpoint = dest_endpoint;
    medium_param.match_info = match_info;
    medium_param.length = length;
    medium_param.lib_cookie = send_lib_cookie;
    memcpy(ep->sendq + 23 * 4096, buffer, length);
    medium_param.sendq_page_offset = 23;

    ret = ioctl(ep->fd, MPOE_CMD_SEND_MEDIUM, &medium_param);
    if (ret < 0) {
      perror("ioctl/send/medium");
      free(req);
      return ret;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_MEDIUM;

  }

  req->generic.state = MPOE_REQUEST_STATE_PENDING;
  req->generic.status.context = context;
  req->send.lib_cookie = send_lib_cookie++;

  mpoe_enqueue_request(&ep->sent_req_q, req);

  *requestp = req;

  return 0;
}

int
mpoe_irecv(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, union mpoe_request **requestp)
{
  union mpoe_request * req;

  req = malloc(sizeof(union mpoe_request));
  if (!req) {
    perror("request malloc");
    return -ENOMEM;
  }

  /* FIXME */

  req->generic.type = MPOE_REQUEST_TYPE_RECV;
  req->generic.state = MPOE_REQUEST_STATE_PENDING;
  req->generic.status.context = context;
  req->recv.buffer = buffer;
  req->recv.length = length;

  mpoe_enqueue_request(&ep->recv_req_q, req);

  *requestp = req;

  return 0;
}

int
mpoe_wait(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status)
{
  union mpoe_request * req = *requestp;

  while (req->generic.state != MPOE_REQUEST_STATE_DONE)
    mpoe_progress(ep);

  memcpy(status, &req->generic.status, sizeof(*status));

  free(req);
  *requestp = NULL;

  return 0;
}
