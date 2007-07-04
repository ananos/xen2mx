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
#include "mpoe_list.h"

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

  INIT_LIST_HEAD(&ep->sent_req_q);
  INIT_LIST_HEAD(&ep->unexp_req_q);
  INIT_LIST_HEAD(&ep->recv_req_q);
  INIT_LIST_HEAD(&ep->multifraq_medium_recv_req_q);
  INIT_LIST_HEAD(&ep->done_req_q);

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
mpoe_enqueue_request(struct list_head *head,
		     union mpoe_request *req)
{
  list_add_tail(&req->generic.queue_elt, head);
}

static inline void
mpoe_dequeue_request(struct list_head *head,
		     union mpoe_request *req)
{
  /* FIXME: under debug, check that req was in the list */
  list_del(&req->generic.queue_elt);
}

static inline union mpoe_request *
mpoe_queue_first_request(struct list_head *head)
{
  return list_first_entry(head, union mpoe_request, generic.queue_elt);
}

static inline int
mpoe_queue_empty(struct list_head *head)
{
  return list_empty(head);
}

static int lib_cookie = 0;
static union mpoe_request * cookie_req;

static inline uint32_t
mpoe_lib_cookie_alloc(struct mpoe_endpoint *ep,
		      union mpoe_request *req)
{
  /* FIXME */
  cookie_req = req;
  req->send.lib_cookie = lib_cookie;
  return lib_cookie++;
}

static inline union mpoe_request *
mpoe_find_request_by_cookie(struct mpoe_endpoint *ep,
			    uint32_t cookie)
{
  /* FIXME */
  return cookie_req;
}

static inline void
mpoe_lib_cookie_free(struct mpoe_endpoint *ep,
		     uint32_t cookie)
{
  /* FIXME */
}

static mpoe_return_t
mpoe_progress(struct mpoe_endpoint * ep)
{
  volatile union mpoe_evt * evt;

  evt = ep->next_event;
  if (evt->generic.type == MPOE_EVT_NONE)
    return MPOE_SUCCESS;

  printf("received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case MPOE_EVT_RECV_TINY: {
    struct mpoe_evt_recv_tiny * event = &((union mpoe_evt *)evt)->recv_tiny;
    union mpoe_request *req;
    unsigned long length;

    if (mpoe_queue_empty(&ep->recv_req_q)) {
      void *unexp_buffer;

      req = malloc(sizeof(*req));
      if (!req) {
	fprintf(stderr, "Failed to allocate request for unexpected tiny messages, dropping\n");
	break;
      }

      length = event->length;
      unexp_buffer = malloc(length);
      if (!unexp_buffer) {
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
      req->recv.buffer = unexp_buffer;

      memcpy(unexp_buffer, (void *) evt->recv_tiny.data, length);

      mpoe_enqueue_request(&ep->unexp_req_q, req);

    } else {
      req = mpoe_queue_first_request(&ep->recv_req_q);
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

    if (mpoe_queue_empty(&ep->recv_req_q)) {
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
      req = mpoe_queue_first_request(&ep->recv_req_q);
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
    struct mpoe_evt_recv_medium * event = &((union mpoe_evt *)evt)->recv_medium;
    union mpoe_request * req;
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * buffer = ep->recvq + evt_index*4096; /* FIXME: get pagesize somehow */
    unsigned long msg_length = event->msg_length;
    unsigned long chunk = event->length;
    unsigned long seqnum = event->seqnum;
    unsigned long offset = seqnum << (10 + event->pipeline); /* FIXME */

    printf("got a medium seqnum %d pipeline %d length %d offset %d of total %d\n",
	   seqnum, event->pipeline, chunk, offset, msg_length);

    if (!mpoe_queue_empty(&ep->multifraq_medium_recv_req_q)) {
      /* message already partially received */

      req = mpoe_queue_first_request(&ep->multifraq_medium_recv_req_q);

      if (req->recv.type.medium.frames_received_mask & (1 << seqnum))
	/* already received this frame */
	break;

      /* take care of the data chunk */
      if (offset + chunk > msg_length)
	chunk = msg_length - offset;
      memcpy(req->recv.buffer + offset, buffer, chunk);
      req->recv.type.medium.frames_received_mask |= 1 << seqnum;
      req->recv.type.medium.accumulated_length += chunk;

      if (req->recv.type.medium.accumulated_length == msg_length) {
	req->generic.state = MPOE_REQUEST_STATE_DONE;
	mpoe_dequeue_request(&ep->multifraq_medium_recv_req_q, req);
	mpoe_enqueue_request(&ep->done_req_q, req);
      }

      /* FIXME: do not duplicate all the code like this */

    } else if (!mpoe_queue_empty(&ep->recv_req_q)) {
      /* first fragment of a new message */

      req = mpoe_queue_first_request(&ep->recv_req_q);
      mpoe_dequeue_request(&ep->recv_req_q, req);

      /* set basic fields */
      mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
      req->generic.status.ep = event->src_endpoint;
      req->generic.status.match_info = event->match_info;

      /* compute message length */
      req->generic.status.msg_length = msg_length;
      if (msg_length > req->recv.length)
	msg_length = req->recv.length;
      req->generic.status.xfer_length = msg_length;

      /* take care of the data chunk */
      if (offset + chunk > msg_length)
	chunk = msg_length - offset;
      memcpy(req->recv.buffer + offset, buffer, chunk);
      req->recv.type.medium.frames_received_mask = 1 << seqnum;
      req->recv.type.medium.accumulated_length = chunk;

      if (chunk == msg_length) {
	req->generic.state = MPOE_REQUEST_STATE_DONE;
	mpoe_enqueue_request(&ep->done_req_q, req);
      } else {
	mpoe_enqueue_request(&ep->multifraq_medium_recv_req_q, req);
      }

    } else {

      printf("missed a medium unexpected\n");
      /* FIXME */
    }
    break;
  }

  case MPOE_EVT_SEND_DONE: {
    uint32_t lib_cookie = evt->send_done.lib_cookie;
    union mpoe_request * req;
    printf("send done, cookie %ld\n", (unsigned long) lib_cookie);
    req = mpoe_find_request_by_cookie(ep, lib_cookie);

    /* if medium not done, do nothing */
    if (req->generic.type == MPOE_REQUEST_TYPE_SEND_MEDIUM
	&& --req->send.type.medium.frames_pending_nr)
      break;

    mpoe_dequeue_request(&ep->sent_req_q, req);
    mpoe_lib_cookie_free(ep, lib_cookie);
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
  mpoe_return_t ret;
  uint32_t lib_cookie;
  int err;

  req = malloc(sizeof(union mpoe_request));
  if (!req) {
    ret = mpoe_errno_to_return(ENOMEM, "isend request malloc");
    goto out;
  }

  lib_cookie = mpoe_lib_cookie_alloc(ep, req);
  /* FIXME: check failed */

  if (length <= MPOE_TINY_MAX) {
    struct mpoe_cmd_send_tiny tiny_param;

    mpoe_mac_addr_copy(&tiny_param.hdr.dest_addr, dest_addr);
    tiny_param.hdr.dest_endpoint = dest_endpoint;
    tiny_param.hdr.match_info = match_info;
    tiny_param.hdr.length = length;
    tiny_param.hdr.lib_cookie = lib_cookie;
    memcpy(tiny_param.data, buffer, length);

    err = ioctl(ep->fd, MPOE_CMD_SEND_TINY, &tiny_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/tiny");
      goto out_with_cookie;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_TINY;

  } else if (length <= MPOE_SMALL_MAX) {
    struct mpoe_cmd_send_small small_param;

    mpoe_mac_addr_copy(&small_param.dest_addr, dest_addr);
    small_param.dest_endpoint = dest_endpoint;
    small_param.match_info = match_info;
    small_param.length = length;
    small_param.lib_cookie = lib_cookie;
    small_param.vaddr = (uintptr_t) buffer;

    err = ioctl(ep->fd, MPOE_CMD_SEND_SMALL, &small_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/small");
      goto out_with_cookie;
    }

    req->generic.type = MPOE_REQUEST_TYPE_SEND_SMALL;

  } else {
    struct mpoe_cmd_send_medium medium_param;
    uint32_t remaining = length;
    uint32_t offset = 0;
    int frames;
    int i;

    frames = (length + 4095) >> 12; /* FIXME */
    mpoe_mac_addr_copy(&medium_param.dest_addr, dest_addr);
    medium_param.dest_endpoint = dest_endpoint;
    medium_param.match_info = match_info;
    medium_param.pipeline = 2; /* always send full pages */
    medium_param.lib_cookie = lib_cookie;
    medium_param.msg_length = length;

    for(i=0; i<frames; i++) {
      unsigned long chunk = remaining > 4096 ? 4096 : remaining;
      medium_param.length = chunk;
      medium_param.seqnum = i;
      medium_param.sendq_page_offset = i;
      printf("sending medium seqnum %d pipeline 2 length %d of total %d\n", i, chunk, length);
      memcpy(ep->sendq + i * 4096, buffer + offset, length);

      err = ioctl(ep->fd, MPOE_CMD_SEND_MEDIUM, &medium_param);
      if (err < 0) {
	ret = mpoe_errno_to_return(errno, "ioctl send/medium");
	goto out_with_cookie;
      }

      remaining -= chunk;
      offset += chunk;
    }

    req->send.type.medium.frames_pending_nr = frames;
    req->generic.type = MPOE_REQUEST_TYPE_SEND_MEDIUM;

  }

  req->generic.state = MPOE_REQUEST_STATE_PENDING;
  req->generic.status.context = context;

  mpoe_enqueue_request(&ep->sent_req_q, req);

  mpoe_progress(ep);

  *requestp = req;

  return MPOE_SUCCESS;

 out_with_cookie:
  mpoe_lib_cookie_free(ep, lib_cookie);
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

  if (!mpoe_queue_empty(&ep->unexp_req_q)) {
    req = mpoe_queue_first_request(&ep->unexp_req_q);
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

  mpoe_progress(ep);

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
    /* FIXME: sleep */
  }

  mpoe_dequeue_request(&ep->done_req_q, req);
  memcpy(status, &req->generic.status, sizeof(*status));

  free(req);
  *requestp = NULL;
  *result = 1;

 out:
  return ret;
}

mpoe_return_t
mpoe_ipeek(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	   uint32_t *result)
{
  mpoe_return_t ret = MPOE_SUCCESS;

  ret = mpoe_progress(ep);
  if (ret != MPOE_SUCCESS)
    goto out;

  if (mpoe_queue_empty(&ep->done_req_q)) {
    *result = 0;
  } else {
    *requestp = mpoe_queue_first_request(&ep->done_req_q);
    *result = 1;
  }

 out:
  return ret;
}

mpoe_return_t
mpoe_peek(struct mpoe_endpoint *ep, union mpoe_request **requestp,
	  uint32_t *result)
{
  mpoe_return_t ret = MPOE_SUCCESS;

  while (mpoe_queue_empty(&ep->done_req_q)) {
    ret = mpoe_progress(ep);
    if (ret != MPOE_SUCCESS)
      goto out;
    /* FIXME: sleep */
  }

  *requestp = mpoe_queue_first_request(&ep->done_req_q);
  *result = 1;

 out:
  return ret;
}

/* FIXME: test/wait_any */
