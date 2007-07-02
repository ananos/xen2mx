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

static mpoe_return_t
mpoe_errno_to_return(int error, char * caller)
{
  switch (error) {
  case EINVAL:
    return MPOE_INVALID_PARAMETER;
  case EACCES:
  case EPERM:
    return MPOE_ACCESS_DENIED;
  case EMFILE:
  case ENFILE:
  case ENOMEM:
    return MPOE_NO_SYSTEM_RESOURCES;
  case ENODEV:
  case ENOENT:
    return MPOE_NO_DEVICE;
  default:
    fprintf(stderr, "MPoE: %s got unexpected errno %d (%s)\n",
	    caller, error, strerror(error));
    return MPOE_BAD_ERROR;
  }
}

const char *
mpoe_strerror(mpoe_return_t ret)
{
  switch (ret) {
  case MPOE_SUCCESS:
    return "Success";
  case MPOE_BAD_ERROR:
    return "Bad (internal?) error";
  case MPOE_NO_DEVICE:
    return "No device";
  case MPOE_ACCESS_DENIED:
    return "Access denied";
  case MPOE_NO_RESOURCES:
    return "No resources available";
  case MPOE_NO_SYSTEM_RESOURCES:
    return "No resources available in the system";
  case MPOE_INVALID_PARAMETER:
    return "Invalid parameter";
  }
  assert(0);
}

const char *
mpoe_strstatus(mpoe_status_code_t code)
{
  switch (code) {
  case MPOE_STATUS_SUCCESS:
    return "Success";
  case MPOE_STATUS_FAILED:
    return "Failed";
  }
  assert(0);
}

mpoe_return_t
mpoe_get_board_count(uint32_t * count)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err, fd;

  err = open(MPOE_DEVNAME, O_RDWR);
  if (err < 0) {
    ret = mpoe_errno_to_return(errno, "open");
    goto out;
  }
  fd = err;

  err = ioctl(fd, MPOE_CMD_GET_BOARD_COUNT, &count);
  if (err < 0) {
    ret = mpoe_errno_to_return(errno, "ioctl GET_BOARD_COUNT");
    goto out_with_fd;
  }

 out_with_fd:
  close(fd);
 out:
  return ret;
}

/* FIXME: get board id */

mpoe_return_t
mpoe_open_endpoint(uint32_t board_index, uint32_t index,
		   struct mpoe_endpoint **epp)
{
  /* FIXME: add parameters to choose the board name? */
  struct mpoe_cmd_open_endpoint open_param;
  struct mpoe_endpoint * ep;
  void * recvq, * sendq, * eventq;
  mpoe_return_t ret = MPOE_SUCCESS;
  int err, fd;

  ep = malloc(sizeof(struct mpoe_endpoint));
  if (!ep) {
    ret = mpoe_errno_to_return(ENOMEM, "endpoint malloc");
    goto out;
  }

  err = open(MPOE_DEVNAME, O_RDWR);
  if (err < 0) {
    ret = mpoe_errno_to_return(errno, "open");
    goto out_with_ep;
  }
  fd = err;

  /* ok */
  open_param.board_index = board_index;
  open_param.endpoint_index = index;
  err = ioctl(fd, MPOE_CMD_OPEN_ENDPOINT, &open_param);
  if (err < 0) {
    ret = mpoe_errno_to_return(errno, "ioctl OPEN_ENDPOINT");
    goto out_with_fd;
  }
  fprintf(stderr, "Successfully attached endpoint %d/%d\n", 0, 34);

  /* mmap */
  sendq = mmap(0, MPOE_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_SENDQ_OFFSET);
  recvq = mmap(0, MPOE_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_RECVQ_OFFSET);
  eventq = mmap(0, MPOE_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_EVENTQ_OFFSET);
  if (sendq == (void *) -1
      || recvq == (void *) -1
      || eventq == (void *) -1) {
    ret = mpoe_errno_to_return(errno, "mmap");
    goto out_with_attached;
  }
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

  return MPOE_SUCCESS;

 out_with_attached:
  /* could detach here, but close will do it */
 out_with_fd:
  close(fd);
 out_with_ep:
  free(ep);
 out:
  return ret;
}

mpoe_return_t
mpoe_close_endpoint(struct mpoe_endpoint *ep)
{
  munmap(ep->sendq, MPOE_SENDQ_SIZE);
  munmap(ep->recvq, MPOE_RECVQ_SIZE);
  munmap(ep->eventq, MPOE_EVENTQ_SIZE);
  close(ep->fd);
  free(ep);

  return MPOE_SUCCESS;
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

static mpoe_return_t
mpoe_progress(struct mpoe_endpoint * ep)
{
  volatile union mpoe_evt * evt;

  evt = ep->next_event;
  while (evt->generic.type == MPOE_EVT_NONE) ;

  printf("received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case MPOE_EVT_RECV_TINY: {
    struct mpoe_evt_recv_tiny * event = &((union mpoe_evt *)evt)->recv_tiny;

    if (!ep->recv_req_q) {
      union mpoe_request *req;
      void *buffer;
      unsigned long length;

      req = malloc(sizeof(*req));
      if (!req) {
	fprintf(stderr, "Failed to allocate request for unexpected tiny messages, dropping\n");
	break;
      }

      length = event->length;
      buffer = malloc(length);
      if (!buffer) {
	fprintf(stderr, "Failed to allocate buffer for unexpected tiny messages, dropping\n");
	free(req);
	break;
      }

      mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
      /* FIXME: set state and status.code? */
      req->generic.status.ep = event->src_endpoint;
      req->generic.status.match_info = event->match_info;
      req->generic.status.msg_length = length;
      req->generic.status.xfer_length = length;
      req->recv.buffer = buffer;

      memcpy(buffer, (void *) evt->recv_tiny.data, length);

      mpoe_enqueue_request(&ep->unexp_req_q, req);

    } else {
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

      mpoe_enqueue_request(&ep->done_req_q, req);
    }
    break;
  }

  case MPOE_EVT_RECV_SMALL: {
    struct mpoe_evt_recv_small * event = &((union mpoe_evt *)evt)->recv_small;
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index*4096; /* FIXME: get pagesize somehow */
    union mpoe_request *req;
    unsigned long length;

    if (!ep->recv_req_q) {
      void *unexp_buffer;

      req = malloc(sizeof(*req));
      if (!req) {
	fprintf(stderr, "Failed to allocate request for unexpected small messages, dropping\n");
	break;
      }

      length = event->length;
      unexp_buffer = malloc(length);
      if (!unexp_buffer) {
	fprintf(stderr, "Failed to allocate buffer for unexpected small messages, dropping\n");
	free(req);
	break;
      }

      mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
      /* FIXME: set state and status.code? */
      req->generic.status.ep = event->src_endpoint;
      req->generic.status.match_info = event->match_info;
      req->generic.status.msg_length = length;
      req->generic.status.xfer_length = length;
      req->recv.buffer = unexp_buffer;

      memcpy(unexp_buffer, recvq_buffer, length);

      mpoe_enqueue_request(&ep->unexp_req_q, req);

    } else {
      req = ep->recv_req_q;

      mpoe_dequeue_request(&ep->recv_req_q, req);
      req->generic.state = MPOE_REQUEST_STATE_DONE;

      mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
      req->generic.status.ep = event->src_endpoint;
      req->generic.status.match_info = event->match_info;

      length = event->length > req->recv.length
	? req->recv.length : event->length;
      req->generic.status.msg_length = event->length;
      req->generic.status.xfer_length = length;
      memcpy(req->recv.buffer, recvq_buffer, length);

      mpoe_enqueue_request(&ep->done_req_q, req);
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

      mpoe_enqueue_request(&ep->done_req_q, req);
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
    mpoe_enqueue_request(&ep->done_req_q, req);
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

  return MPOE_SUCCESS;
}

mpoe_return_t
mpoe_isend(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info,
	   struct mpoe_mac_addr *dest_addr, uint32_t dest_endpoint,
	   void *context, union mpoe_request **requestp)
{
  union mpoe_request * req;
  static int send_lib_cookie = 0;
  mpoe_return_t ret;
  int err;

  req = malloc(sizeof(union mpoe_request));
  if (!req) {
    ret = mpoe_errno_to_return(ENOMEM, "isend request malloc");
    goto out;
  }

  if (length <= MPOE_TINY_MAX) {
    struct mpoe_cmd_send_tiny tiny_param;

    mpoe_mac_addr_copy(&tiny_param.hdr.dest_addr, dest_addr);
    tiny_param.hdr.dest_endpoint = dest_endpoint;
    tiny_param.hdr.match_info = match_info;
    tiny_param.hdr.length = length;
    tiny_param.hdr.lib_cookie = send_lib_cookie;
    memcpy(tiny_param.data, buffer, length);

    err = ioctl(ep->fd, MPOE_CMD_SEND_TINY, &tiny_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/tiny");
      goto out_with_req;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_TINY;

  } else if (length <= MPOE_SMALL_MAX) {
    struct mpoe_cmd_send_small small_param;

    mpoe_mac_addr_copy(&small_param.dest_addr, dest_addr);
    small_param.dest_endpoint = dest_endpoint;
    small_param.match_info = match_info;
    small_param.length = length;
    small_param.lib_cookie = send_lib_cookie;
    small_param.vaddr = (uintptr_t) buffer;

    err = ioctl(ep->fd, MPOE_CMD_SEND_SMALL, &small_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/small");
      goto out_with_req;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_SMALL;

  } else {
    struct mpoe_cmd_send_medium medium_param;

    mpoe_mac_addr_copy(&medium_param.dest_addr, dest_addr);
    medium_param.dest_endpoint = dest_endpoint;
    medium_param.match_info = match_info;
    medium_param.length = length;
    medium_param.lib_cookie = send_lib_cookie;
    memcpy(ep->sendq + 23 * 4096, buffer, length);
    medium_param.sendq_page_offset = 23;

    err = ioctl(ep->fd, MPOE_CMD_SEND_MEDIUM, &medium_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/medium");
      goto out_with_req;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_MEDIUM;

  }

  req->generic.state = MPOE_REQUEST_STATE_PENDING;
  req->generic.status.context = context;
  req->send.lib_cookie = send_lib_cookie++;

  mpoe_enqueue_request(&ep->sent_req_q, req);

  *requestp = req;

  return MPOE_SUCCESS;

 out_with_req:
  free(req);
 out:
  return ret;
}

mpoe_return_t
mpoe_irecv(struct mpoe_endpoint *ep,
	   void *buffer, size_t length,
	   uint64_t match_info, uint64_t match_mask,
	   void *context, union mpoe_request **requestp)
{
  union mpoe_request * req;
  mpoe_return_t ret;

  if ((req = ep->unexp_req_q) != NULL) {
    mpoe_dequeue_request(&ep->sent_req_q, req);

    if (length > req->recv.length)
      length = req->recv.length;
    memcpy(buffer, req->recv.buffer, length);
    free(req->recv.buffer);
    req->recv.buffer = buffer;

    req->generic.status.xfer_length = length;
    req->generic.state = MPOE_REQUEST_STATE_DONE;

    mpoe_enqueue_request(&ep->done_req_q, req);

  } else {
    req = malloc(sizeof(union mpoe_request));
    if (!req) {
      ret = mpoe_errno_to_return(ENOMEM, "irecv request malloc");
      goto out;
    }

    req->generic.type = MPOE_REQUEST_TYPE_RECV;
    req->generic.state = MPOE_REQUEST_STATE_PENDING;
    req->generic.status.context = context;
    req->recv.buffer = buffer;
    req->recv.length = length;

    mpoe_enqueue_request(&ep->recv_req_q, req);
  }

  *requestp = req;

  return MPOE_SUCCESS;

 out:
  return ret;
}

mpoe_return_t
mpoe_test(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status, uint32_t * result)
{
  union mpoe_request * req = *requestp;
  mpoe_return_t ret = MPOE_SUCCESS;

  ret = mpoe_progress(ep);
  if (ret != MPOE_SUCCESS)
    goto out;

  if (req->generic.state != MPOE_REQUEST_STATE_DONE) {
    *result = 0;
  } else {
    mpoe_dequeue_request(&ep->done_req_q, req);
    memcpy(status, &req->generic.status, sizeof(*status));

    free(req);
    *requestp = NULL;
    *result = 1;
  }

 out:
  return ret;
}

mpoe_return_t
mpoe_wait(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  struct mpoe_status *status, uint32_t * result)
{
  union mpoe_request * req = *requestp;
  mpoe_return_t ret = MPOE_SUCCESS;

  while (req->generic.state != MPOE_REQUEST_STATE_DONE) {
    ret = mpoe_progress(ep);
    if (ret != MPOE_SUCCESS)
      goto out;
  }

  mpoe_dequeue_request(&ep->done_req_q, req);

  memcpy(status, &req->generic.status, sizeof(*status));

  free(req);
  *requestp = NULL;
  *result = 1;

 out:
  return ret;
}
