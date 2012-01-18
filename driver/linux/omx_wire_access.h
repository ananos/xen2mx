/*
 * Open-MX
 * Copyright © inria 2007-2009 (see AUTHORS file)
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

#ifdef OMX_ENDIAN_COMPAT
#define OMX__HTON_8(_field)		((__u8) (_field))
#define OMX__HTON_16(_field)		((__force __u16) htons(_field))
#define OMX__HTON_32(_field)		((__force __u32) htonl(_field))
#define OMX__NTOH_8(_pkt_field)		((__u8) (_pkt_field))
#define OMX__NTOH_16(_pkt_field)	(ntohs((__force __be16) _pkt_field))
#define OMX__NTOH_32(_pkt_field)	(ntohl((__force __be32) _pkt_field))
#else /* !OMX_ENDIAN_COMPAT */
#define OMX__HTON_8(_field)		((__u8) (_field))
#define OMX__HTON_16(_field)		((__u16) (_field))
#define OMX__HTON_32(_field)		((__u32) (_field))
#define OMX__NTOH_8(_pkt_field)		((__u8) (_pkt_field))
#define OMX__NTOH_16(_pkt_field)	((__u16) (_pkt_field))
#define OMX__NTOH_32(_pkt_field)	((__u32) (_pkt_field))
#endif /* !OMX_ENDIAN_COMPAT */

#define OMX_HTON_8(_pkt_field, _field)	do {	\
	BUILD_BUG_ON(sizeof(_pkt_field) != 1);	\
	_pkt_field = OMX__HTON_8(_field);	\
} while (0)

#define OMX_HTON_16(_pkt_field, _field) do {	\
	BUILD_BUG_ON(sizeof(_pkt_field) != 2);	\
	_pkt_field = OMX__HTON_16(_field);	\
} while (0)

#define OMX_HTON_32(_pkt_field, _field) do {	\
	BUILD_BUG_ON(sizeof(_pkt_field) != 4);	\
	_pkt_field = OMX__HTON_32(_field);	\
} while (0)

#define OMX_NTOH_8(_pkt_field) ({		\
	BUILD_BUG_ON(sizeof(_pkt_field) != 1);	\
	OMX__NTOH_8(_pkt_field);		\
})

#define OMX_NTOH_16(_pkt_field) ({		\
	BUILD_BUG_ON(sizeof(_pkt_field) != 2);	\
	OMX__NTOH_16(_pkt_field);		\
})

#define OMX_NTOH_32(_pkt_field) ({		\
	BUILD_BUG_ON(sizeof(_pkt_field) != 4);	\
	OMX__NTOH_32(_pkt_field);		\
})

#define OMX_HTON_MATCH_INFO(_pkt, _match_info) do {				\
	OMX_HTON_32((_pkt)->match_a, (uint32_t) (_match_info >> 32));		\
	OMX_HTON_32((_pkt)->match_b, (uint32_t) (_match_info & 0xffffffff));	\
} while (0)

#define OMX_NTOH_MATCH_INFO(_pkt)			\
 ((((uint64_t) OMX_NTOH_32((_pkt)->match_a)) << 32)	\
  | ((uint64_t) OMX_NTOH_32((_pkt)->match_b)))

#endif /* __omx_wire_access_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
