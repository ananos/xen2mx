/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#ifndef __omx_lib_h__
#define __omx_lib_h__

#ifdef OMX_LIB_DEBUG
#define MALLOC_CHECK_ 3
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "open-mx.h"
#include "omx_types.h"
#include "omx_lib_wire.h"
#include "omx_io.h"
#include "omx_valgrind.h"

/************
 * Debugging
 */

#ifdef OMX_LIB_DEBUG

#define OMX_VERBOSE_MAIN (1<<0)
#define OMX_VERBOSE_ENDPOINT (1<<1)
#define OMX_VERBOSE_CONNECT (1<<2)
#define OMX_VERBOSE_SEND (1<<3)
#define OMX_VERBOSE_LARGE (1<<4)
#define OMX_VERBOSE_MEDIUM (1<<5)
#define OMX_VERBOSE_SEQNUM (1<<6)
#define OMX_VERBOSE_RECV (1<<7)
#define OMX_VERBOSE_UNEXP (1<<8)
#define OMX_VERBOSE_EARLY (1<<9)
#define OMX_VERBOSE_ACK (1<<10)
#define OMX_VERBOSE_EVENT (1<<11)
#define OMX_VERBOSE_WAIT (1<<12)
#define OMX_VERBOSE_VECT (1<<13)
#define omx__verbose_type_enabled(type) (OMX_VERBOSE_##type & omx__globals.verbose)

#define INLINE
#define omx__debug_assert(x) assert(x)
#define omx__debug_instr(x) do { x; } while (0)
#define omx__debug_printf(type,args...) do { if (omx__verbose_type_enabled(type)) fprintf(stderr, args); } while (0)

#else /* OMX_LIB_DEBUG */

#define INLINE inline
#define omx__debug_assert(x) /* nothing */
#define omx__debug_instr(x) /* nothing */
#define omx__debug_printf(type,args...) /* nothing */

#endif /* OMX_LIB_DEBUG */

#define omx__abort(args...) do { fprintf(stderr, "Open-MX fatal error: " args); assert(0); } while (0)

/*************
 * Optimizing
 */

#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && (__GNUC__ > 2 || __GNUC__ == 2 && __GNUC_MINOR__ >= 96)
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#else
#define likely(x)	(x)
#define unlikely(x)	(x)
#endif

/*****************
 * Various macros
 */

#define OMX_DEFAULT_DEVNAME "/dev/open-mx"
#ifndef OMX_DEVNAME
#define OMX_DEVNAME OMX_DEFAULT_DEVNAME
#endif

#define OMX_MEDIUM_FRAG_PIPELINE 12 /* always send 4k pages (1<<12) */
#define OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT OMX_MEDIUM_FRAG_PIPELINE
#define OMX_MEDIUM_FRAG_LENGTH_MAX (1<<OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)
#define OMX_MEDIUM_FRAGS_NR(len) ((len+OMX_MEDIUM_FRAG_LENGTH_MAX-1)>>OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)

/******************
 * Various globals
 */

extern struct omx__globals omx__globals;
extern volatile struct omx_driver_desc * omx__driver_desc;

/******************
 * Timing routines
 */

#define ACK_PER_SECOND 64 /* simplifies divisions too */
#define omx__ack_delay_jiffies() ((omx__driver_desc->hz + ACK_PER_SECOND) / ACK_PER_SECOND)

#define RESEND_PER_SECOND 2
#define omx__resend_delay_jiffies() ((omx__driver_desc->hz + RESEND_PER_SECOND) / RESEND_PER_SECOND)

/* assume 1s = 1024ms, to simplify divisions */

#define omx__timeout_ms_to_resends(ms) ((ms * RESEND_PER_SECOND + 1023) / 1024)

static inline uint64_t
omx__timeout_ms_to_relative_jiffies(uint32_t ms)
{
	uint32_t hz = omx__driver_desc->hz;
	return (ms == OMX_TIMEOUT_INFINITE)
		? OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE
		: (ms * hz + 1023)/1024;
}

static inline uint64_t
omx__timeout_ms_to_absolute_jiffies(uint32_t ms)
{
	uint32_t hz = omx__driver_desc->hz;
	uint64_t now = omx__driver_desc->jiffies;
	return (ms == OMX_TIMEOUT_INFINITE)
		? OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE
		: now + (ms * hz + 1023)/1024;
}

/*********************
 * Internal functions
 */

extern omx_return_t
omx__progress(struct omx_endpoint * ep);

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
omx__partner_localization_shared(struct omx__partner *partner)
{
  omx__debug_assert(partner->localization != OMX__PARTNER_LOCALIZATION_UNKNOWN);
#ifdef OMX_DISABLE_SHARED
  return 0;
#else
  return (partner->localization == OMX__PARTNER_LOCALIZATION_LOCAL);
#endif
}

extern void
omx__partner_cleanup(struct omx_endpoint *ep,
		     struct omx__partner *partner, int disconnect);

extern omx_return_t
omx__connect_myself(struct omx_endpoint *ep, uint64_t board_addr);

extern omx_return_t
omx__partner_recv_lookup(struct omx_endpoint *ep,
			 uint16_t peer_index, uint8_t endpoint_index,
			 struct omx__partner ** partnerp);

extern void
omx__post_connect(struct omx_endpoint *ep,
		  struct omx__partner *partner,
		  union omx_request * req);

extern omx_return_t
omx__process_recv_connect(struct omx_endpoint *ep,
			  struct omx_evt_recv_connect *event);

extern void
omx__connect_complete(struct omx_endpoint *ep, union omx_request *req,
		      omx_status_code_t status);

omx_return_t
omx__connect_wait(omx_endpoint_t ep, union omx_request * req,
		  uint32_t ms_timeout);

extern omx_return_t
omx__endpoint_large_region_map_init(struct omx_endpoint * ep);

extern void
omx__endpoint_large_region_map_exit(struct omx_endpoint * ep);

omx_return_t
omx__get_region(struct omx_endpoint *ep,
		struct omx__req_seg *segs,
		struct omx__large_region **regionp,
		void * reserver);

omx_return_t
omx__put_region(struct omx_endpoint *ep,
		struct omx__large_region *region,
		void * reserver);

void
omx__post_notify(struct omx_endpoint *ep,
		 struct omx__partner *partner,
		 union omx_request * req);

omx_return_t
omx__submit_isend_medium(struct omx_endpoint *ep,
			     union omx_request *req);

omx_return_t
omx__submit_isend_rndv(struct omx_endpoint *ep,
		       union omx_request *req);

omx_return_t
omx__submit_notify(struct omx_endpoint *ep,
		   union omx_request *req);

void
omx__queue_notify(struct omx_endpoint *ep,
		  union omx_request *req);

omx_return_t
omx__submit_pull(struct omx_endpoint * ep,
	       union omx_request * req);

extern omx_return_t
omx__submit_or_queue_pull(struct omx_endpoint * ep,
			  union omx_request * req);

extern omx_return_t
omx__process_self_send(struct omx_endpoint *ep,
		       union omx_request *sreq);

extern void
omx__send_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_status_code_t status);

extern void
omx__recv_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_status_code_t status);

extern omx_return_t
omx__process_recv(struct omx_endpoint *ep,
		  struct omx_evt_recv_msg *msg, void *data, uint32_t msg_length,
		  omx__process_recv_func_t recv_func);

extern void
omx__process_recv_tiny(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       struct omx_evt_recv_msg *msg,
		       void *data /* unused */, uint32_t msg_length);

extern void
omx__process_recv_small(struct omx_endpoint *ep, struct omx__partner *partner,
			union omx_request *req,
			struct omx_evt_recv_msg *msg,
			void *data, uint32_t msg_length);

extern void
omx__process_recv_medium_frag(struct omx_endpoint *ep, struct omx__partner *partner,
			      union omx_request *req,
			      struct omx_evt_recv_msg *msg,
			      void *data, uint32_t msg_length);

extern void
omx__process_recv_rndv(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       struct omx_evt_recv_msg *msg,
		       void *data /* unused */, uint32_t msg_length);

extern void
omx__process_recv_notify(struct omx_endpoint *ep, struct omx__partner *partner,
			 union omx_request *req,
			 struct omx_evt_recv_msg *msg,
			 void *data /* unused */, uint32_t msg_length /* unused */);

extern omx_return_t
omx__process_pull_done(struct omx_endpoint * ep,
		       struct omx_evt_pull_done * event);

omx_return_t
omx__process_recv_truc(struct omx_endpoint *ep,
		       struct omx_evt_recv_truc *truc);

extern omx_return_t
omx__handle_ack(struct omx_endpoint *ep,
		struct omx__partner *partner, omx__seqnum_t ack);

extern void
omx__handle_truc_ack(struct omx_endpoint *ep,
		     struct omx__partner *partner,
		     struct omx__truc_ack_data *ack_n);

extern omx_return_t
omx__handle_nack(struct omx_endpoint *ep,
                 struct omx__partner *partner, omx__seqnum_t seqnum,
                 omx_status_code_t status);

extern void
omx__mark_request_acked(struct omx_endpoint *ep,
			union omx_request *req,
			omx_status_code_t status);

extern void
omx__process_resend_requests(struct omx_endpoint *ep);

extern void
omx__process_queued_requests(struct omx_endpoint *ep);

extern omx_return_t
omx__process_partners_to_ack(struct omx_endpoint *ep);

extern omx_return_t
omx__flush_partners_to_ack(struct omx_endpoint *ep);

void
omx__prepare_progress_wakeup(struct omx_endpoint *ep);

static inline void
omx__partner_needs_to_ack(struct omx_endpoint *ep,
			  struct omx__partner *partner)
{
  if (!partner->oldest_recv_time_not_acked) {
    partner->oldest_recv_time_not_acked = omx__driver_desc->jiffies;
    list_add_tail(&partner->endpoint_partners_to_ack_elt, &ep->partners_to_ack_list);
  }
}

static inline void
omx__partner_ack_sent(struct omx_endpoint *ep,
		      struct omx__partner *partner)
{
  if (partner->oldest_recv_time_not_acked) {
    partner->oldest_recv_time_not_acked = 0;
    list_del(&partner->endpoint_partners_to_ack_elt);
  }
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

extern omx_return_t omx__errno_to_return(char * caller);

extern omx_return_t omx__get_board_count(uint32_t * count);
extern omx_return_t omx__get_board_info(struct omx_endpoint * ep, uint8_t index, struct omx_board_info * info);
extern omx_return_t omx__get_board_index_by_name(const char * name, uint8_t * index);

extern omx_return_t omx__driver_peer_add(uint64_t board_addr, char *hostname);
extern omx_return_t omx__driver_peers_clear(void);
extern omx_return_t omx__peers_dump(const char * format);
extern omx_return_t omx__peer_addr_to_index(uint64_t board_addr, uint16_t *index);
extern omx_return_t omx__peer_index_to_addr(uint16_t index, uint64_t *board_addrp);

static inline int
omx__endpoint_sendq_map_get(struct omx_endpoint * ep,
			    int nr, void * user, int * founds)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  int index, i;

  omx__debug_assert((ep->sendq_map.first_free == -1) == (ep->sendq_map.nr_free == 0));

  if (unlikely(ep->sendq_map.nr_free < nr))
    return -1;

  index = ep->sendq_map.first_free;
  for(i=0; i<nr; i++) {
    int next_free;

    omx__debug_assert(index >= 0);

    next_free = array[index].next_free;

    omx__debug_assert(array[index].user == NULL);
    omx__debug_instr(array[index].next_free = -1);

    array[index].user = user;
    founds[i] = index;
    index = next_free;
  }
  ep->sendq_map.first_free = index;
  ep->sendq_map.nr_free -= nr;

  return 0;
}

static inline void
omx__endpoint_sendq_map_put(struct omx_endpoint * ep,
			    int nr, int *indexes)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  void * user;
  int i;

  user = array[indexes[0]].user;
#ifdef OMX_LIB_DEBUG
  for(i=1; i<nr; i++)
    if (user != array[indexes[i]].user)
      omx__abort("Tried to put some unrelated sendq map entries\n");
#endif

  omx__debug_assert(user != NULL);

  for(i=0; i<nr; i++) {
    omx__debug_assert(array[indexes[i]].next_free == -1);

    array[indexes[i]].user = NULL;
    array[indexes[i]].next_free = i ? indexes[i-1] : ep->sendq_map.first_free;
  }

  ep->sendq_map.first_free = indexes[nr-1];
  ep->sendq_map.nr_free += nr;
}

static inline void *
omx__endpoint_sendq_map_user(struct omx_endpoint * ep,
			     int index)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  void * user = array[index].user;

  omx__debug_assert(user != NULL);
  omx__debug_assert(array[index].next_free == -1);

  return user;
}

#endif /* __omx_lib_h__ */
