/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

#include "omx_io.h"
#include "omx_wire.h"

#include <linux/if_ether.h>
#include <linux/if.h>

/*
 * This file runs build-time assertions without ever being linked to anybody
 */

#define CHECK(x) do { char (*a)[(x) ? 1 : -1] = NULL; (void) a; } while (0)

extern void assertions(void); /* shut-up the sparse checker */

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
  CHECK(OMX_UNEXP_EVENTQ_ENTRY_NR == OMX_RECVQ_ENTRY_NR);
  CHECK((unsigned) OMX_PKT_TYPE_MAX == (1<<(sizeof(omx_packet_type_t)*8)) - 1);

  CHECK(OMX_PKT_TYPE_MAX <= 255); /* uint8_t is used on the wire */
  CHECK(OMX_NACK_TYPE_MAX <= 255); /* uint8_t is used on the wire */
  CHECK(OMX_EVT_NACK_LIB_BAD_ENDPT == OMX_NACK_TYPE_BAD_ENDPT);
  CHECK(OMX_EVT_NACK_LIB_ENDPT_CLOSED == OMX_NACK_TYPE_ENDPT_CLOSED);
  CHECK(OMX_EVT_NACK_LIB_BAD_SESSION == OMX_NACK_TYPE_BAD_SESSION);

  CHECK(OMX_EVT_PULL_DONE_BAD_ENDPT == OMX_NACK_TYPE_BAD_ENDPT);
  CHECK(OMX_EVT_PULL_DONE_ENDPT_CLOSED == OMX_NACK_TYPE_ENDPT_CLOSED);
  CHECK(OMX_EVT_PULL_DONE_BAD_SESSION == OMX_NACK_TYPE_BAD_SESSION);
  CHECK(OMX_EVT_PULL_DONE_BAD_RDMAWIN == OMX_NACK_TYPE_BAD_RDMAWIN);

  /* make sure we can always dereference omx_pkt_head and ptype in incoming skb */
  CHECK(ETH_ZLEN >= sizeof(struct omx_pkt_head));
  CHECK(ETH_ZLEN >= OMX_HDR_PTYPE_OFFSET + sizeof(omx_packet_type_t));
}
