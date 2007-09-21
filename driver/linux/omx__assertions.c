/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#include "omx_types.h"
#include "omx_io.h"

#include <linux/if.h>

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = 0; (void) a; } while (0)

void
assertions(void)
{
  CHECK(OMX_IF_NAMESIZE == IFNAMSIZ);
  CHECK(sizeof(uint64_t) >= sizeof(((struct ethhdr *)NULL)->h_dest));
  CHECK(sizeof(uint64_t) >= sizeof(((struct ethhdr *)NULL)->h_source));
  CHECK(PAGE_SIZE%OMX_SENDQ_ENTRY_SIZE == 0);
  CHECK(PAGE_SIZE%OMX_RECVQ_ENTRY_SIZE == 0);
  CHECK(OMX_SENDQ_ENTRY_SIZE <= OMX_RECVQ_ENTRY_SIZE);
  CHECK(sizeof(union omx_evt) == OMX_EVENTQ_ENTRY_SIZE);
  CHECK((unsigned) OMX_PKT_TYPE_MAX == (1<<(sizeof(((struct omx_pkt_msg*)NULL)->ptype)*8)) - 1);
}
