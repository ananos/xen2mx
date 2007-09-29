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

#ifndef __omx_wire_access_h__
#define __omx_wire_access_h__

#ifndef __KERNEL__
#include <arpa/inet.h>
#endif

#define OMX_PKT_FIELD_FROM(_pkt_field, _field)					\
do {										\
	if (sizeof(_pkt_field) == 1)						\
		_pkt_field = (uint8_t) (_field);				\
	else if (sizeof(_pkt_field) == 2)					\
		_pkt_field = (typeof(_pkt_field)) htons((uint16_t) (_field));	\
	else									\
		_pkt_field = (typeof(_pkt_field)) htonl((uint32_t) (_field));	\
} while (0)

#define OMX_FROM_PKT_FIELD(_pkt_field)	\
( sizeof(_pkt_field) == 1		\
  ? (_pkt_field)			\
  : ( sizeof(_pkt_field) == 2		\
      ? ntohs(_pkt_field)		\
      : ntohl(_pkt_field)		\
     )					\
)

#define OMX_PKT_MATCH_INFO_FROM(_pkt, _match_info)					\
do {											\
	OMX_PKT_FIELD_FROM((_pkt)->match_a, (uint32_t) (_match_info >> 32));		\
	OMX_PKT_FIELD_FROM((_pkt)->match_b, (uint32_t) (_match_info & 0xffffffff));	\
} while (0)

#define OMX_FROM_PKT_MATCH_INFO(_pkt)				\
 (((uint64_t) OMX_FROM_PKT_FIELD((_pkt)->match_a)) << 32)	\
 | ((uint64_t) OMX_FROM_PKT_FIELD((_pkt)->match_b))

#endif /* __omx_wire_access_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
