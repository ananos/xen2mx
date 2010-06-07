/*
 * Open-MX
 * Copyright © INRIA 2007-2010 (see AUTHORS file)
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

#ifndef __omx__valgrind_h__
#define __omx__valgrind_h__

#if defined OMX_LIB_DEBUG && defined OMX_LIB_VALGRIND_DEBUG

/*
 * Valgrind support to check memory access and allocation.
 * Use "valgrind --sim-hints=lax-ioctls myprogram" to check your program
 * (and this library). If using an old valgrind, "--sim-hints" might have
 * to be replaced with "--weird-hacks".
 */

#include <valgrind/memcheck.h>

/* Mark a memory buffer as non-accessible, accessible or accessible+initialized */
#define OMX_VALGRIND_MEMORY_MAKE_NOACCESS(p, s) VALGRIND_MAKE_MEM_NOACCESS(p, s)
#define OMX_VALGRIND_MEMORY_MAKE_WRITABLE(p, s) VALGRIND_MAKE_MEM_UNDEFINED(p, s)
#define OMX_VALGRIND_MEMORY_MAKE_READABLE(p, s) VALGRIND_MAKE_MEM_DEFINED(p, s)

/* Check that memory is defined or writable */
#define OMX_VALGRIND_CHECK_DEFINED(p, s) VALGRIND_CHECK_VALUE_IS_DEFINED(p, s)
#define OMX_VALGRIND_CHECK_WRITABLE(p, s) VALGRIND_CHECK_VALUE_IS_WRITABLE(p, s)

#else /* !VALGRIND_NONDEBUG && (!DEBUG || !VALGRIND_DEBUG) */

#define OMX_VALGRIND_MEMORY_MAKE_NOACCESS(p, s) /* nothing */
#define OMX_VALGRIND_MEMORY_MAKE_WRITABLE(p, s) /* nothing */
#define OMX_VALGRIND_MEMORY_MAKE_READABLE(p, s) /* nothing */

#define OMX_VALGRIND_CHECK_DEFINED(p, s) /* nothing */
#define OMX_VALGRIND_CHECK_WRITABLE(p, s) /* nothing */

#endif /* DEBUG && VALGRIND_DEBUG */

#endif /* __omx__valgrind_h__ */
