/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

#define OMX_SEG_PTR_SET(_seg, _ptr) do { (_seg)->vaddr = (uintptr_t) (_ptr); } while (0)
#define OMX_SEG_PTR(_seg) ((void *)(uintptr_t) (_seg)->vaddr)

static inline void
omx_cache_single_segment(struct omx__req_segs * reqsegs, void * buffer, uint32_t length)
{
  OMX_SEG_PTR_SET(&reqsegs->single, buffer);
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

    reqsegs->segs = malloc(nseg * sizeof(struct omx_cmd_user_segment));
    if (!reqsegs->segs)
      /* the caller checks error codes */
      return OMX_NO_RESOURCES;

    reqsegs->nseg = nseg;
    reqsegs->total_length = 0;
    for(i=0; i<nseg; i++) {
      OMX_SEG_PTR_SET(&reqsegs->segs[i], segs[i].ptr);
      reqsegs->segs[i].len = segs[i].len;
      reqsegs->total_length += segs[i].len;
    }
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
omx_clone_segments(struct omx__req_segs * dst, struct omx__req_segs * src)
{
  memcpy(dst, src, sizeof(*src));
  if (src->nseg == 1)
    dst->segs = &dst->single;
}

static inline void
omx_copy_from_segments(void *dst, struct omx__req_segs *srcsegs, uint32_t length)
{
  omx__debug_assert(length <= srcsegs->total_length);

  if (likely(srcsegs->nseg == 1)) {
    memcpy(dst, OMX_SEG_PTR(&srcsegs->single), length);
  } else {
    struct omx_cmd_user_segment * cseg = &srcsegs->segs[0];
    while (length) {
      uint32_t chunk = cseg->len > length ? length : cseg->len;
      memcpy(dst, OMX_SEG_PTR(cseg), chunk);
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
    memcpy(OMX_SEG_PTR(&dstsegs->single), src, length);
  } else {
    struct omx_cmd_user_segment * cseg = &dstsegs->segs[0];
    while (length) {
      uint32_t chunk = cseg->len > length ? length : cseg->len;
      memcpy(OMX_SEG_PTR(cseg), src, chunk);
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
    omx_copy_to_segments(dstsegs, OMX_SEG_PTR(&srcsegs->single), length);

  } else if (likely(dstsegs->nseg == 1)) {
    omx_copy_from_segments(OMX_SEG_PTR(&dstsegs->single), srcsegs, length);

  } else {
    struct omx_cmd_user_segment * csseg = &srcsegs->segs[0];
    int cssegoff = 0;
    struct omx_cmd_user_segment * cdseg = &dstsegs->segs[0];
    int cdsegoff = 0;

    while (length) {
      uint32_t chunk = length;
      if (csseg->len < chunk)
	chunk = csseg->len;
      if (cdseg->len < chunk)
	chunk = cdseg->len;

      memcpy(OMX_SEG_PTR(cdseg) + cdsegoff, OMX_SEG_PTR(csseg) + cssegoff, chunk);
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
  struct omx_cmd_user_segment * curseg = state->seg;
  uint32_t curoff = state->offset;

  omx__debug_assert(srcsegs->nseg > 1);

  while (1) {
    uint32_t curchunk = curseg->len - curoff; /* remaining data in the segment */
    uint32_t chunk = curchunk > length ? length : curchunk; /* data to take */
    memcpy(dst, OMX_SEG_PTR(curseg) + curoff, chunk);
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
  struct omx_cmd_user_segment * curseg = state->seg;
  uint32_t curoff = state->offset;

  while (1) {
    uint32_t curchunk = curseg->len - curoff; /* remaining data in the segment */
    uint32_t chunk = curchunk > length ? length : curchunk; /* data to take */
    memcpy(OMX_SEG_PTR(curseg) + curoff, src, chunk);
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
    struct omx_cmd_user_segment * curseg = &dstsegs->segs[0];
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
