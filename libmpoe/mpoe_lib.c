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

#undef MPOE_DEBUG
//#define MPOE_DEBUG 1

#ifdef MPOE_DEBUG
#define mpoe_debug_assert(x) assert(x)
#define mpoe_debug_instr(x) do { x; } while (0)
#else
#define mpoe_debug_assert(x) /* nothing */
#define mpoe_debug_instr(x) /* nothing */
#endif

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

static inline int
mpoe_endpoint_sendq_map_init(struct mpoe_endpoint * ep)
{
  struct mpoe_sendq_entry * array;
  int i;

  array = malloc(MPOE_SENDQ_ENTRY_NR * sizeof(struct mpoe_sendq_entry));
  if (!array)
    return -ENOMEM;

  ep->sendq_map.array = array;

  for(i=0; i<MPOE_SENDQ_ENTRY_NR; i++) {
    array[i].user = NULL;
    array[i].next_free = i+1;
  }
  array[MPOE_SENDQ_ENTRY_NR-1].next_free = -1;
  ep->sendq_map.first_free = 0;
  ep->sendq_map.nr_free = MPOE_SENDQ_ENTRY_NR;

  return 0;
}

static inline void
mpoe_endpoint_sendq_map_exit(struct mpoe_endpoint * ep)
{
  free(ep->sendq_map.array);
}

static inline int
mpoe_endpoint_sendq_map_get(struct mpoe_endpoint * ep,
			    int nr, void * user, int * founds)
{
  struct mpoe_sendq_entry * array = ep->sendq_map.array;
  int index, i;

  mpoe_debug_assert((ep->sendq_map.first_free == -1) == (ep->sendq_map.nr_free == 0));

  if (ep->sendq_map.nr_free < nr)
    return -1;

  index = ep->sendq_map.first_free;
  for(i=0; i<nr; i++) {
    int next_free;

    mpoe_debug_assert(index >= 0);

    next_free = array[index].next_free;

    mpoe_debug_assert(array[index].user == NULL);
    mpoe_debug_instr(array[index].next_free = -1);

    array[index].user = user;
    founds[i] = index;
    index = next_free;
  }
  ep->sendq_map.first_free = index;
  ep->sendq_map.nr_free -= nr;

  return 0;
}

static inline void *
mpoe_endpoint_sendq_map_put(struct mpoe_endpoint * ep,
			    int index)
{
  struct mpoe_sendq_entry * array = ep->sendq_map.array;
  void * user = array[index].user;

  mpoe_debug_assert(user != NULL);
  mpoe_debug_assert(array[index].next_free == -1);

  array[index].user = NULL;
  array[index].next_free = ep->sendq_map.first_free;
  ep->sendq_map.first_free = index;
  ep->sendq_map.nr_free++;

  return user;
}

static inline void
mpoe_partner_init(struct mpoe_partner *partner)
{
  INIT_LIST_HEAD(&partner->partialq);
  partner->next_send_seq = 0;
  partner->next_match_recv_seq = 0;
  partner->next_frag_recv_seq = 0;
}

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

  /* prepare the sendq */
  err = mpoe_endpoint_sendq_map_init(ep);
  if (err < 0) {
    ret = mpoe_errno_to_return(-err, "sendq_map init");
    goto out_with_attached;
  }

  /* mmap */
  sendq = mmap(0, MPOE_SENDQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_SENDQ_FILE_OFFSET);
  recvq = mmap(0, MPOE_RECVQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_RECVQ_FILE_OFFSET);
  eventq = mmap(0, MPOE_EVENTQ_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, MPOE_EVENTQ_FILE_OFFSET);
  if (sendq == (void *) -1
      || recvq == (void *) -1
      || eventq == (void *) -1) {
    ret = mpoe_errno_to_return(errno, "mmap");
    goto out_with_sendq_map;
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

  mpoe_partner_init(&ep->partner);

  *epp = ep;

  return MPOE_SUCCESS;

 out_with_sendq_map:
  mpoe_endpoint_sendq_map_exit(ep);
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
#ifdef MPOE_DEBUG
  struct list_head *e;
  list_for_each(e, head)
    if (req == list_entry(e, union mpoe_request, generic.queue_elt))
      goto found;
  assert(0);

 found:
#endif /* MPOE_DEBUG */
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

typedef mpoe_return_t (*mpoe_process_recv_func_t) (struct mpoe_endpoint *ep,
						   union mpoe_evt *evt,
						   void *data);

static mpoe_return_t
mpoe_process_recv_tiny(struct mpoe_endpoint *ep,
		       union mpoe_evt *evt, void *data)
{
  struct mpoe_evt_recv_tiny *event = &evt->recv_tiny;
  union mpoe_request *req;
  unsigned long length;

  if (mpoe_queue_empty(&ep->recv_req_q)) {
    void *unexp_buffer;

    req = malloc(sizeof(*req));
    if (!req) {
      fprintf(stderr, "Failed to allocate request for unexpected tiny messages, dropping\n");
      return MPOE_NO_RESOURCES;
    }

    length = event->length;
    unexp_buffer = malloc(length);
    if (!unexp_buffer) {
      fprintf(stderr, "Failed to allocate buffer for unexpected tiny messages, dropping\n");
      free(req);
      return MPOE_NO_RESOURCES;
    }

    mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
    req->generic.status.ep = event->src_endpoint;
    req->generic.status.match_info = event->match_info;
    req->generic.status.msg_length = length;
    req->recv.buffer = unexp_buffer;

    memcpy(unexp_buffer, data, length);

    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->unexp_req_q, req);

  } else {
    req = mpoe_queue_first_request(&ep->recv_req_q);
    mpoe_dequeue_request(&ep->recv_req_q, req);

    mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
    req->generic.status.ep = event->src_endpoint;
    req->generic.status.match_info = event->match_info;

    length = event->length > req->recv.length
      ? req->recv.length : event->length;
    req->generic.status.msg_length = event->length;
    req->generic.status.xfer_length = length;
    memcpy(req->recv.buffer, data, length);

    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->done_req_q, req);
  }

  return MPOE_SUCCESS;
}

static mpoe_return_t
mpoe_process_recv_small(struct mpoe_endpoint *ep,
			union mpoe_evt *evt, void *data)
{
  struct mpoe_evt_recv_small * event = &evt->recv_small;
  union mpoe_request *req;
  unsigned long length;

  if (mpoe_queue_empty(&ep->recv_req_q)) {
    void *unexp_buffer;

    req = malloc(sizeof(*req));
    if (!req) {
      fprintf(stderr, "Failed to allocate request for unexpected small messages, dropping\n");
      return MPOE_NO_RESOURCES;
    }

    length = event->length;
    unexp_buffer = malloc(length);
    if (!unexp_buffer) {
      fprintf(stderr, "Failed to allocate buffer for unexpected small messages, dropping\n");
      free(req);
      return MPOE_NO_RESOURCES;
    }

    mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
    req->generic.status.ep = event->src_endpoint;
    req->generic.status.match_info = event->match_info;
    req->generic.status.msg_length = length;
    req->recv.buffer = unexp_buffer;

    memcpy(unexp_buffer, data, length);

    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->unexp_req_q, req);

  } else {
    req = mpoe_queue_first_request(&ep->recv_req_q);
    mpoe_dequeue_request(&ep->recv_req_q, req);

    mpoe_mac_addr_copy(&req->generic.status.mac, &event->src_addr);
    req->generic.status.ep = event->src_endpoint;
    req->generic.status.match_info = event->match_info;

    length = event->length > req->recv.length
      ? req->recv.length : event->length;
    req->generic.status.msg_length = event->length;
    req->generic.status.xfer_length = length;
    memcpy(req->recv.buffer, data, length);

    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->done_req_q, req);
  }

  return MPOE_SUCCESS;
}

static mpoe_return_t
mpoe_process_recv_medium(struct mpoe_endpoint *ep,
			 union mpoe_evt *evt, void *data)
{
  struct mpoe_evt_recv_medium * event = &evt->recv_medium;
  union mpoe_request * req;
  unsigned long msg_length = event->msg_length;
  unsigned long chunk = event->frag_length;
  unsigned long seqnum = event->frag_seqnum;
  unsigned long offset = seqnum << (MPOE_MEDIUM_FRAG_PIPELINE_BASE + event->frag_pipeline);

  printf("got a medium frag seqnum %d pipeline %d length %d offset %d of total %d\n",
	 seqnum, event->frag_pipeline, chunk, offset, msg_length);

  if (!mpoe_queue_empty(&ep->multifraq_medium_recv_req_q)) {
    /* message already partially received */

    req = mpoe_queue_first_request(&ep->multifraq_medium_recv_req_q);

    if (req->recv.type.medium.frags_received_mask & (1 << seqnum))
      /* already received this frag */
      return MPOE_SUCCESS;

    /* take care of the data chunk */
    if (offset + chunk > msg_length)
      chunk = msg_length - offset;
    memcpy(req->recv.buffer + offset, data, chunk);
    req->recv.type.medium.frags_received_mask |= 1 << seqnum;
    req->recv.type.medium.accumulated_length += chunk;

    if (req->recv.type.medium.accumulated_length == msg_length) {
      mpoe_dequeue_request(&ep->multifraq_medium_recv_req_q, req);
      req->generic.state = MPOE_REQUEST_STATE_DONE;
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
    memcpy(req->recv.buffer + offset, data, chunk);
    req->recv.type.medium.frags_received_mask = 1 << seqnum;
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

  return MPOE_SUCCESS;
}

static mpoe_return_t
mpoe_process_recv(struct mpoe_endpoint *ep,
		  union mpoe_evt *evt, mpoe_seqnum_t seqnum, void *data,
		  mpoe_process_recv_func_t recv_func)
{
  printf("got seqnum %d\n", seqnum);

  /* FIXME: check order, do matching, handle unexpected and early */

  return recv_func(ep, evt, data);
}

static mpoe_return_t
mpoe_process_event(struct mpoe_endpoint * ep, union mpoe_evt * evt)
{
  mpoe_return_t ret = MPOE_SUCCESS;

  printf("received type %d\n", evt->generic.type);
  switch (evt->generic.type) {

  case MPOE_EVT_RECV_TINY: {
    ret = mpoe_process_recv(ep,
			    evt, evt->recv_tiny.seqnum, evt->recv_tiny.data,
			    mpoe_process_recv_tiny);
    break;
  }

  case MPOE_EVT_RECV_SMALL: {
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index * MPOE_RECVQ_ENTRY_SIZE;
    ret = mpoe_process_recv(ep,
			    evt, evt->recv_small.seqnum, recvq_buffer,
			    mpoe_process_recv_small);
    break;
  }

  case MPOE_EVT_RECV_MEDIUM: {
    int evt_index = ((char *) evt - (char *) ep->eventq)/sizeof(*evt);
    char * recvq_buffer = ep->recvq + evt_index * MPOE_RECVQ_ENTRY_SIZE;
    ret = mpoe_process_recv(ep,
			    evt, evt->recv_medium.seqnum, recvq_buffer,
			    mpoe_process_recv_medium);
    break;
  }

  case MPOE_EVT_SEND_MEDIUM_FRAG_DONE: {
    uint16_t sendq_page_offset = evt->send_medium_frag_done.sendq_page_offset;
    union mpoe_request * req = mpoe_endpoint_sendq_map_put(ep, sendq_page_offset);
    assert(req
	   && req->generic.type == MPOE_REQUEST_TYPE_SEND_MEDIUM);

    /* message is not done */
    if (--req->send.type.medium.frags_pending_nr)
      break;

    mpoe_dequeue_request(&ep->sent_req_q, req);
    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->done_req_q, req);
    break;
  }

  default:
    printf("unknown type\n");
    assert(0);
  }

  return ret;
}

static mpoe_return_t
mpoe_progress(struct mpoe_endpoint * ep)
{
  /* process events */
  while (1) {
    volatile union mpoe_evt * evt = ep->next_event;

    if (evt->generic.type == MPOE_EVT_NONE)
      break;

    mpoe_process_event(ep, (union mpoe_evt *) evt);

    /* mark event as done */
    evt->generic.type = MPOE_EVT_NONE;

    /* next event */
    evt++;
    if ((void *) evt >= ep->eventq + MPOE_EVENTQ_SIZE)
      evt = ep->eventq;
    ep->next_event = (void *) evt;
  }

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
  mpoe_seqnum_t seqnum;
  mpoe_return_t ret;
  int err;

  req = malloc(sizeof(union mpoe_request));
  if (!req) {
    ret = mpoe_errno_to_return(ENOMEM, "isend request malloc");
    goto out;
  }

  seqnum = ep->partner.next_send_seq++; /* FIXME: increater at the end, in case of error */
  req->send.seqnum = seqnum;

  if (length <= MPOE_TINY_MAX) {
    struct mpoe_cmd_send_tiny tiny_param;

    mpoe_mac_addr_copy(&tiny_param.hdr.dest_addr, dest_addr);
    tiny_param.hdr.dest_endpoint = dest_endpoint;
    tiny_param.hdr.match_info = match_info;
    tiny_param.hdr.length = length;
    tiny_param.hdr.seqnum = seqnum;
    /* FIXME: tiny_param.hdr.lib_cookie = lib_cookie; */
    memcpy(tiny_param.data, buffer, length);

    err = ioctl(ep->fd, MPOE_CMD_SEND_TINY, &tiny_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/tiny");
      goto out_with_req;
    }
    /* no need to wait for a done event, tiny is synchronous */

    req->generic.type = MPOE_REQUEST_TYPE_SEND_TINY;
    req->generic.status.context = context;
    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->done_req_q, req);

  } else if (length <= MPOE_SMALL_MAX) {
    struct mpoe_cmd_send_small small_param;

    mpoe_mac_addr_copy(&small_param.dest_addr, dest_addr);
    small_param.dest_endpoint = dest_endpoint;
    small_param.match_info = match_info;
    small_param.length = length;
    /* FIXME: small_param.lib_cookie = lib_cookie; */
    small_param.vaddr = (uintptr_t) buffer;
    small_param.seqnum = seqnum;

    err = ioctl(ep->fd, MPOE_CMD_SEND_SMALL, &small_param);
    if (err < 0) {
      ret = mpoe_errno_to_return(errno, "ioctl send/small");
      goto out_with_req;
    }
    /* no need to wait for a done event, small is synchronous */

    req->generic.type = MPOE_REQUEST_TYPE_SEND_SMALL;
    req->generic.status.context = context;
    req->generic.state = MPOE_REQUEST_STATE_DONE;
    mpoe_enqueue_request(&ep->done_req_q, req);

  } else {
    struct mpoe_cmd_send_medium medium_param;
    uint32_t remaining = length;
    uint32_t offset = 0;
    int sendq_index[8];
    int frags;
    int i;

    frags = MPOE_MEDIUM_FRAGS_NR(length);
    mpoe_debug_assert(frags <= 8); /* for the sendq_index array above */

    if (mpoe_endpoint_sendq_map_get(ep, frags, req, sendq_index) < 0)
      /* FIXME: queue */
      assert(0);

    mpoe_mac_addr_copy(&medium_param.dest_addr, dest_addr);
    medium_param.dest_endpoint = dest_endpoint;
    medium_param.match_info = match_info;
    medium_param.frag_pipeline = MPOE_MEDIUM_FRAG_PIPELINE;
    /* FIXME: medium_param.lib_cookie = lib_cookie; */
    medium_param.msg_length = length;
    medium_param.seqnum = seqnum;

    for(i=0; i<frags; i++) {
      unsigned long chunk = remaining > MPOE_MEDIUM_FRAG_LENGTH_MAX
	? MPOE_MEDIUM_FRAG_LENGTH_MAX : remaining;
      medium_param.frag_length = chunk;
      medium_param.frag_seqnum = i;
      medium_param.sendq_page_offset = sendq_index[i];
      printf("sending medium seqnum %d pipeline 2 length %d of total %d\n", i, chunk, length);
      memcpy(ep->sendq + (sendq_index[i] << MPOE_MEDIUM_FRAG_LENGTH_MAX_SHIFT), buffer + offset, length);

      err = ioctl(ep->fd, MPOE_CMD_SEND_MEDIUM, &medium_param);
      if (err < 0) {
	ret = mpoe_errno_to_return(errno, "ioctl send/medium");
	goto out_with_req;
      }

      remaining -= chunk;
      offset += chunk;
    }

    /* need to wait for a done event, since the sendq pages
     * might still be in use
     */
    req->send.type.medium.frags_pending_nr = frags;
    req->generic.type = MPOE_REQUEST_TYPE_SEND_MEDIUM;
    req->generic.status.context = context;
    req->generic.state = MPOE_REQUEST_STATE_PENDING;
    mpoe_enqueue_request(&ep->sent_req_q, req);
  }

  mpoe_progress(ep);

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

  if (!mpoe_queue_empty(&ep->unexp_req_q)) {
    req = mpoe_queue_first_request(&ep->unexp_req_q);
    mpoe_dequeue_request(&ep->sent_req_q, req);

    /* compute xfer length */
    if (length > req->generic.status.msg_length)
      length = req->generic.status.msg_length;
    req->generic.status.xfer_length = length;

    /* copy data from the unexpected buffer */
    memcpy(buffer, req->recv.buffer, length);
    free(req->recv.buffer);

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
