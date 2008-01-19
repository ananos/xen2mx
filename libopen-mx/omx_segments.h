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

#ifndef __omx_segments_h__
#define __omx_segments_h__

#include <stdlib.h>

#include "omx_lib.h"
#include "omx_types.h"

static inline void
omx_cache_single_segment(struct omx__req_seg * reqsegs, void * buffer, uint32_t length)
{
  reqsegs->single.ptr = buffer;
  reqsegs->single.len = length;
  reqsegs->nseg = 1;
  reqsegs->segs = &reqsegs->single;
  reqsegs->total_length = reqsegs->single.len;
}

static inline omx_return_t
omx_cache_segments(struct omx__req_seg * reqsegs, omx_seg_t * segs, uint32_t nseg)
{

  if (nseg == 0) {
    /* use a single empty buffer, to avoid having to check for nsegs>0 */
    omx_cache_single_segment(reqsegs, NULL, 0);

  } else if (nseg == 1) {
    omx_cache_single_segment(reqsegs, segs[0].ptr, segs[0].len);

  } else {
    int i;

    reqsegs->segs = malloc(nseg * sizeof(omx_seg_t));
    if (!reqsegs->segs)
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
omx_free_segments(struct omx__req_seg * reqsegs)
{
  if (unlikely(reqsegs->nseg > 1))
    free(reqsegs->segs);
}

static inline void
omx_copy_from_segments(void *dst, struct omx__req_seg *srcsegs, uint32_t length)
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
omx_copy_to_segments(struct omx__req_seg *dstsegs, void *src, uint32_t length)
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

#endif /* __omx_segments_h__ */
