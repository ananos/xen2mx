#ifndef __omx__valgrind_h__
#define __omx__valgrind_h__

#ifdef OMX_VALGRIND_DEBUG

/*
 * Valgrind support to check memory access and allocation.
 * Use "valgrind --sim-hints=lax-ioctls myprogram" to check your program
 * (and this library). If using an old valgrind, "--sim-hints" might have
 * to be replaced with "--weird-hacks".
 */

#include <valgrind/memcheck.h>

/* Mark a memory buffer as non-accessible, accessible or accessible+initialized */
#define MPOE_VALGRIND_MEMORY_MAKE_NOACCESS(p, s) VALGRIND_MAKE_NOACCESS(p, s)
#define MPOE_VALGRIND_MEMORY_MAKE_WRITABLE(p, s) VALGRIND_MAKE_WRITABLE(p, s)
#define MPOE_VALGRIND_MEMORY_MAKE_READABLE(p, s) VALGRIND_MAKE_READABLE(p, s)

/* FIXME: ioctl pre/post hooks */

#else /* !OMX_VALGRIND_DEBUG */

#define MPOE_VALGRIND_MEMORY_MAKE_NOACCESS(p, s) /* nothing */
#define MPOE_VALGRIND_MEMORY_MAKE_WRITABLE(p, s) /* nothing */
#define MPOE_VALGRIND_MEMORY_MAKE_READABLE(p, s) /* nothing */

#endif /* !OMX_VALGRIND_DEBUG */

#endif /* __omx__valgrind_h__ */
