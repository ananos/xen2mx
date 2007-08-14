/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "open-mx.h"
#include "omx_types.h"
#include "omx_io.h"
#include "omx_valgrind.h"

/************
 * Debugging
 */

#ifdef OMX_DEBUG
#define omx__debug_assert(x) assert(x)
#define omx__debug_instr(x) do { x; } while (0)
#define omx__debug_printf(args...) fprintf(stderr, args)
#else
#define omx__debug_assert(x) /* nothing */
#define omx__debug_instr(x) /* nothing */
#define omx__debug_printf(args...) /* nothing */
#endif

/*****************
 * Various macros
 */

#define OMX_DEVNAME "/dev/open-mx"
/* FIXME: envvar to configure? */

#define OMX_MEDIUM_FRAG_PIPELINE_BASE 10  /* pipeline is encoded -10 on the wire */
#define OMX_MEDIUM_FRAG_PIPELINE 2 /* always send 4k pages (1<<(10+2)) */
#define OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT (OMX_MEDIUM_FRAG_PIPELINE_BASE+OMX_MEDIUM_FRAG_PIPELINE)
#define OMX_MEDIUM_FRAG_LENGTH_MAX (1<<OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)
#define OMX_MEDIUM_FRAGS_NR(len) ((len+OMX_MEDIUM_FRAG_LENGTH_MAX-1)>>OMX_MEDIUM_FRAG_LENGTH_MAX_SHIFT)

/******************
 * Various globals
 */

extern struct omx__globals omx__globals;

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

extern omx_return_t
omx__connect_myself(struct omx_endpoint *ep, uint64_t board_addr);

extern omx_return_t
omx__partner_recv_lookup(struct omx_endpoint *ep,
			 uint16_t peer_index, uint8_t endpoint_index,
			 struct omx__partner ** partnerp);

extern omx_return_t
omx__process_recv_connect(struct omx_endpoint *ep,
			  struct omx_evt_recv_connect *event);

omx_return_t
omx__register_region(struct omx_endpoint *ep,
		     char * buffer, size_t length,
		     struct omx__large_region *region);

omx_return_t
omx__deregister_region(struct omx_endpoint *ep,
		       struct omx__large_region *region);

extern omx_return_t
omx__queue_large_recv(struct omx_endpoint * ep,
		      union omx_request * req);

extern omx_return_t
omx__notify(struct omx_endpoint * ep,
	    union omx_request * req);

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
extern omx_return_t omx__get_board_id(struct omx_endpoint * ep, uint8_t * index, char * name, uint64_t * addr);
extern omx_return_t omx__get_board_index_by_name(const char * name, uint8_t * index);

extern omx_return_t omx__peers_init(void);
extern omx_return_t omx__peers_dump(const char * format);
extern omx_return_t omx__peer_addr_to_index(uint64_t board_addr, uint16_t *index);
extern omx_return_t omx__peer_from_index(uint16_t index, uint64_t *board_addr, char **hostname);

static inline int
omx__endpoint_sendq_map_get(struct omx_endpoint * ep,
			    int nr, void * user, int * founds)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  int index, i;

  omx__debug_assert((ep->sendq_map.first_free == -1) == (ep->sendq_map.nr_free == 0));

  if (ep->sendq_map.nr_free < nr)
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

static inline void *
omx__endpoint_sendq_map_put(struct omx_endpoint * ep,
			    int index)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  void * user = array[index].user;

  omx__debug_assert(user != NULL);
  omx__debug_assert(array[index].next_free == -1);

  array[index].user = NULL;
  array[index].next_free = ep->sendq_map.first_free;
  ep->sendq_map.first_free = index;
  ep->sendq_map.nr_free++;

  return user;
}

#endif /* __omx_lib_h__ */
