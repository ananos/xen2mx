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

#include <net/if.h>

#include "omx_io.h"
#include "omx_lib.h"

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = 0; (void) a; } while (0)

void
assertions(void)
{
  CHECK(sizeof(struct omx_evt_recv_msg) == OMX_EVENTQ_ENTRY_SIZE);
  CHECK(sizeof(union omx_evt) == OMX_EVENTQ_ENTRY_SIZE);
  CHECK(OMX_IF_NAMESIZE == IF_NAMESIZE);
  CHECK(OMX_MEDIUM_FRAG_LENGTH_MAX <= OMX_RECVQ_ENTRY_SIZE);
  CHECK(OMX_MEDIUM_FRAG_LENGTH_MAX <= OMX_SENDQ_ENTRY_SIZE);
}


#ifdef OMX_MX_API_COMPAT

#include "myriexpress.h"
#include "mx_extensions.h"

void
compat_assertions(void)
{
  /* check the contents of status types, since their fields are different */
  CHECK(sizeof(mx_status_t) == sizeof(omx_status_t));
  CHECK(offsetof(mx_status_t, code) == offsetof(omx_status_t, code));
  CHECK(sizeof(((mx_status_t*)NULL)->code) == sizeof(((omx_status_t*)NULL)->code));
  CHECK(offsetof(mx_status_t, source) == offsetof(omx_status_t, addr));
  CHECK(sizeof(((mx_status_t*)NULL)->source) == sizeof(((omx_status_t*)NULL)->addr));
  CHECK(offsetof(mx_status_t, match_info) == offsetof(omx_status_t, match_info));
  CHECK(sizeof(((mx_status_t*)NULL)->match_info) == sizeof(((omx_status_t*)NULL)->match_info));
  CHECK(offsetof(mx_status_t, msg_length) == offsetof(omx_status_t, msg_length));
  CHECK(sizeof(((mx_status_t*)NULL)->msg_length) == sizeof(((omx_status_t*)NULL)->msg_length));
  CHECK(offsetof(mx_status_t, xfer_length) == offsetof(omx_status_t, xfer_length));
  CHECK(sizeof(((mx_status_t*)NULL)->xfer_length) == sizeof(((omx_status_t*)NULL)->xfer_length));
  CHECK(offsetof(mx_status_t, context) == offsetof(omx_status_t, context));
  CHECK(sizeof(((mx_status_t*)NULL)->context) == sizeof(((omx_status_t*)NULL)->context));

  /* check the contents of segment types, since their fields are different */
  CHECK(sizeof(mx_segment_t) == sizeof(omx_seg_t));
  CHECK(offsetof(mx_segment_t, segment_ptr) == offsetof(omx_seg_t, ptr));
  CHECK(sizeof(((mx_segment_t*)NULL)->segment_ptr) == sizeof(((omx_seg_t*)NULL)->ptr));
  CHECK(offsetof(mx_segment_t, segment_length) == offsetof(omx_seg_t, len));
  CHECK(sizeof(((mx_segment_t*)NULL)->segment_length) == sizeof(((omx_seg_t*)NULL)->len));

  /* check the size of enums */
  CHECK(sizeof(mx_return_t) == sizeof(omx_return_t));
  CHECK(sizeof(mx_status_code_t) == sizeof(omx_status_code_t));
}

#endif
