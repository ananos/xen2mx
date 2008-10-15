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

#ifndef __omx_segments_h__
#define __omx_segments_h__

#include <stdlib.h>

#include "omx_lib.h"
#include "omx_types.h"

static inline void
omx_cache_single_segment(struct omx__req_segs * reqsegs, void * buffer, uint32_t length)
{
  reqsegs->single.ptr = buffer;
  reqsegs->single.len = length;
  reqsegs->nseg = 1;
  reqsegs->segs = &reqsegs->single;
  reqsegs->total_length = reqsegs->single.len;
}

static inline omx_return_t
omx_cache_segments(struct omx__req_segs * reqsegs, omx_seg_t * segs, uint32_t nseg)
{

  if (nseg == 0) {
    /* use a single empty buffer, to avoid having to check for nsegs>0 */
    omx_cache_single_segment(reqsegs, NULL, 0);

  } else if (nseg == 1) {
    omx_cache_single_segment(reqsegs, segs[0].ptr, segs[0].len);

  } else {
    int i;

    if (nseg > OMX_MAX_SEGMENTS)
      /* the caller checks error codes */
      return OMX_SEGMENTS_BAD_COUNT;

    reqsegs->segs = malloc(nseg * sizeof(omx_seg_t));
    if (!reqsegs->segs)
      /* the caller checks error codes */
      return OMX_NO_RESOURCES;

    memcpy(reqsegs->segs, segs, nseg * sizeof(omx_seg_t));

    reqsegs->total_length = 0;
    for(i=0; i<nseg; i++)
      reqsegs->total_length += segs[i].len;

    reqsegs->nseg = nseg;
  }

  return OMX_SUCCESS;
}

static inline void
omx_free_segments(struct omx__req_segs * reqsegs)
{
  if (unlikely(reqsegs->nseg > 1))
    free(reqsegs->segs);
}

static inline void
omx_copy_from_segments(void *dst, struct omx__req_segs *srcsegs, uint32_t length)
{
  omx__debug_assert(length <= srcsegs->total_length);

  if (likely(srcsegs->nseg == 1)) {
    memcpy(dst, srcsegs->single.ptr, length);
  } else {
    omx_seg_t * cseg = &srcsegs->segs[0];
    while (length) {
      uint32_t chunk = cseg->len > length ? length : cseg->len;
      memcpy(dst, cseg->ptr, chunk);
      dst += chunk;
      length -= chunk;
      cseg++;
    }
  }
}

static inline void
omx_copy_to_segments(struct omx__req_segs *dstsegs, void *src, uint32_t length)
{
  omx__debug_assert(length <= dstsegs->total_length);

  if (likely(dstsegs->nseg == 1)) {
    memcpy(dstsegs->single.ptr, src, length);
  } else {
    omx_seg_t * cseg = &dstsegs->segs[0];
    while (length) {
      uint32_t chunk = cseg->len > length ? length : cseg->len;
      memcpy(cseg->ptr, src, chunk);
      src += chunk;
      length -= chunk;
      cseg++;
    }
  }
}

static inline void
omx_copy_from_to_segments(struct omx__req_segs *dstsegs, struct omx__req_segs *srcsegs, uint32_t length)
{
  omx__debug_assert(length <= dstsegs->total_length);
  omx__debug_assert(length <= srcsegs->total_length);

  if (likely(srcsegs->nseg == 1)) {
    omx_copy_to_segments(dstsegs, srcsegs->single.ptr, length);

  } else if (likely(dstsegs->nseg == 1)) {
    omx_copy_from_segments(dstsegs->single.ptr, srcsegs, length);

  } else {
    omx_seg_t * csseg = &srcsegs->segs[0];
    int cssegoff = 0;
    omx_seg_t * cdseg = &dstsegs->segs[0];
    int cdsegoff = 0;

    while (length) {
      uint32_t chunk = length;
      if (csseg->len < chunk)
	chunk = csseg->len;
      if (cdseg->len < chunk)
	chunk = cdseg->len;

      memcpy(cdseg->ptr + cdsegoff, csseg->ptr + cssegoff, chunk);
      length -= chunk;

      cssegoff += chunk;
      if (cssegoff >= csseg->len) {
	csseg++;
	cssegoff = 0;
      }

      cdsegoff += chunk;
      if (cdsegoff >= cdseg->len) {
	cdseg++;
	cdsegoff = 0;
      }
    }
  }
}

/*
 * copy a chunk of segments into a contigous buffer,
 * start at state and update state before returning
 */
static inline void
omx_continue_partial_copy_from_segments(struct omx_endpoint *ep,
					void *dst, struct omx__req_segs *srcsegs,
					uint32_t length,
					struct omx_segscan_state *state)
{
  omx_seg_t * curseg = state->seg;
  uint32_t curoff = state->offset;

  omx__debug_assert(srcsegs->nseg > 1);

  while (1) {
    uint32_t curchunk = curseg->len - curoff; /* remaining data in the segment */
    uint32_t chunk = curchunk > length ? length : curchunk; /* data to take */
    memcpy(dst, curseg->ptr + curoff, chunk);
    omx__debug_printf(VECT, ep, "copying %ld from seg %d at %ld\n",
		      (unsigned long) chunk, (unsigned) (curseg-&srcsegs->segs[0]), (unsigned long)curoff);
    length -= chunk;            
    dst += chunk;
    if (curchunk != chunk) {
      /* we didn't consume this whole segment, we're done */
      curoff += chunk;
      break;
    } else {
      /* next segment, and exit if nothing to do anymore */
      curseg++;
      curoff = 0;
      if (!length)
	break;
    }
  }

  state->seg = curseg;
  state->offset = curoff;
}

/*
 * copy a chunk of contigous buffer into segments,
 * start at state and update state before returning
 */
static inline void
omx_continue_partial_copy_to_segments(struct omx_endpoint *ep,
				      struct omx__req_segs *dstsegs, void *src,
				      uint32_t length,
				      struct omx_segscan_state *state)
{
  omx_seg_t * curseg = state->seg;
  uint32_t curoff = state->offset;

  while (1) {
    uint32_t curchunk = curseg->len - curoff; /* remaining data in the segment */
    uint32_t chunk = curchunk > length ? length : curchunk; /* data to take */
    memcpy(curseg->ptr + curoff, src, chunk);
    omx__debug_printf(VECT, ep, "copying %ld into seg %d at %ld\n",
		      (unsigned long) chunk, (unsigned) (curseg-&dstsegs->segs[0]), (unsigned long)curoff);
    length -= chunk;            
    src += chunk;
    if (curchunk != chunk) {
      /* we didn't consume this whole segment, we're done */
      curoff += chunk;
      break;
    } else {
      /* next segment, and exit if nothing to do anymore */
      curseg++;
      curoff = 0;
      if (!length)
	break;
    }
  }

  state->seg = curseg;
  state->offset = curoff;
}

/*
 * copy a chunk of contigous buffer into segments,
 * check whether the saved state is valid and use it, or update it first.
 * then, start at state and update state before returning.
 */
static inline void
omx_partial_copy_to_segments(struct omx_endpoint *ep,
			     struct omx__req_segs *dstsegs, void *src,
			     uint32_t length,
			     uint32_t offset, struct omx_segscan_state *scan_state, uint32_t *scan_offset)
{
  if (offset != *scan_offset) {
    omx_seg_t * curseg = &dstsegs->segs[0];
    uint32_t curoffset = 0;
    while (offset > curoffset + curseg->len) {
      curoffset += curseg->len;
      curseg++;
    }
    scan_state->seg = curseg;
    scan_state->offset = offset - curoffset;
  }

  omx_continue_partial_copy_to_segments(ep, dstsegs, src, length, scan_state);
  *scan_offset = offset+length;
}

#endif /* __omx_segments_h__ */
