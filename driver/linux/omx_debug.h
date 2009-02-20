/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

#ifndef __omx_debug_h__
#define __omx_debug_h__

#if (defined OMX_DRIVER_DEBUG) || (defined OMX_DRIVER_PROFILING)
#define INLINE
#else
#define INLINE inline
#endif

#ifdef OMX_DRIVER_DEBUG

#define OMX_DEBUG_SEND (1<<0)
#define OMX_DEBUG_RECV (1<<1)
#define OMX_DEBUG_DROP (1<<2)
#define OMX_DEBUG_PULL (1<<3)
#define OMX_DEBUG_REG (1<<4)
#define OMX_DEBUG_IOCTL (1<<5)
#define OMX_DEBUG_EVENT (1<<6)
#define OMX_DEBUG_PEER (1<<7)
#define OMX_DEBUG_KREF (1<<8)
#define OMX_DEBUG_DMA (1<<9)
#define OMX_DEBUG_QUERY (1<<10)
#define OMX_DEBUG_MMU (1<<11)

extern unsigned long omx_debug;
#define omx_debug_type_enabled(type) (OMX_DEBUG_##type & omx_debug)

#define dprintk(type, x...) do { if (omx_debug_type_enabled(type)) printk(KERN_INFO "OMXdbg-" #type ": " x); } while (0)

#else /* !OMX_DRIVER_DEBUG */
#define dprintk(type, x...) do { /* nothing */ } while (0)
#endif /* !OMX_DRIVER_DEBUG */

#define omx_send_dprintk(_eh, _format, ...) \
dprintk(SEND, \
	"Open-MX: sending from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__)

#define omx_recv_dprintk(_eh, _format, ...) \
dprintk(RECV, \
	"Open-MX: received from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	(_eh)->h_dest[0], (_eh)->h_dest[1], (_eh)->h_dest[2], \
	(_eh)->h_dest[3], (_eh)->h_dest[4], (_eh)->h_dest[5], \
	##__VA_ARGS__);

#define omx_drop_dprintk(_eh, _format, ...) \
dprintk(DROP, \
	"Open-MX: dropping pkt from %02x:%02x:%02x:%02x:%02x:%02x, " _format "\n", \
	(_eh)->h_source[0], (_eh)->h_source[1], (_eh)->h_source[2], \
	(_eh)->h_source[3], (_eh)->h_source[4], (_eh)->h_source[5], \
	##__VA_ARGS__);

#endif /* __omx_debug_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
