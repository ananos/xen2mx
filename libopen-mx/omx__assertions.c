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
  CHECK(OMX_MEDIUM_FRAG_LENGTH_MAX <= OMX_RECVQ_ENTRY_SIZE);
  CHECK(OMX_MEDIUM_FRAG_LENGTH_MAX <= OMX_SENDQ_ENTRY_SIZE);
}
