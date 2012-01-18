/*
 * Open-MX
 * Copyright © inria 2010 (see AUTHORS file)
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

#ifndef __omx_debug_h__
#define __omx_debug_h__


#define omx__message_prefix(ep) ((ep) ? ((struct omx_endpoint *) ep)->message_prefix : omx__globals.message_prefix)

#define omx__error_sleeps() do {								\
  if (omx__globals.abort_sleeps) {								\
    fprintf(stderr, "Open-MX sleeping %d before aborting, you may attach with gdb -p %ld\n",	\
	    omx__globals.abort_sleeps, (unsigned long) getpid());				\
    sleep(omx__globals.abort_sleeps);								\
  }												\
} while (0)

#define omx__printf(ep, format, ...) do { fprintf(stderr, "%s" format, omx__message_prefix(ep), ##__VA_ARGS__); } while (0)
#define omx__verbose_printf(ep, format, ...) do { if (omx__globals.verbose) omx__printf(ep, format, ##__VA_ARGS__); } while (0)
#define omx__warning(ep, format, ...) do { omx__printf(ep, "WARNING: " format, ##__VA_ARGS__); } while (0)
#define omx__abort(ep, format, ...) do {			\
  omx__printf(ep, "FatalError: " format, ##__VA_ARGS__);	\
  omx__error_sleeps();						\
  assert(0);							\
} while (0)

#ifdef OMX_LIB_DEBUG

#define OMX_VERBDEBUG_ENDPOINT (1<<1)
#define OMX_VERBDEBUG_CONNECT (1<<2)
#define OMX_VERBDEBUG_SEND (1<<3)
#define OMX_VERBDEBUG_LARGE (1<<4)
#define OMX_VERBDEBUG_MEDIUM (1<<5)
#define OMX_VERBDEBUG_SEQNUM (1<<6)
#define OMX_VERBDEBUG_RECV (1<<7)
#define OMX_VERBDEBUG_UNEXP (1<<8)
#define OMX_VERBDEBUG_EARLY (1<<9)
#define OMX_VERBDEBUG_ACK (1<<10)
#define OMX_VERBDEBUG_EVENT (1<<11)
#define OMX_VERBDEBUG_WAIT (1<<12)
#define OMX_VERBDEBUG_VECT (1<<13)
#define omx__verbdebug_type_enabled(type) (OMX_VERBDEBUG_##type & omx__globals.verbdebug)

#define INLINE
#define omx__debug_assert(x) assert(x)
#define omx__debug_instr(x) do { x; } while (0)
#define omx__debug_printf(type, ep, format, ...) do { if (omx__verbdebug_type_enabled(type)) omx__printf(ep, format, ##__VA_ARGS__); } while (0)

#else /* !OMX_LIB_DEBUG */

#define INLINE inline
#define omx__debug_assert(x) (void)(x)
#define omx__debug_instr(x) do { /* nothing */ } while (0)
#define omx__debug_printf(type, format, ...) do { /* nothing */ } while (0)

#endif /* !OMX_LIB_DEBUG */

extern void omx__debug_init(int signum);

#define BUILD_BUG_ON(condition) ((void)(sizeof(struct { int:-!!(condition); })))


#endif /* __omx_debug_h__ */
