/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

#ifndef __omx_threads_h__
#define __omx_threads_h__

#ifdef OMX_LIB_THREAD_SAFETY

#include <pthread.h>

struct omx__lock {
  pthread_mutex_t _mutex;
};

struct omx__cond {
  pthread_cond_t _cond;
};

#define OMX__LOCK_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }
#define omx__lock_init(lock) pthread_mutex_init(&(lock)->_mutex, NULL)
#define omx__lock_destroy(lock) pthread_mutex_destroy(&(lock)->_mutex)
#define omx__lock(lock) pthread_mutex_lock(&(lock)->_mutex)
#define omx__unlock(lock) pthread_mutex_unlock(&(lock)->_mutex)

#define omx__cond_init(cond) pthread_cond_init(&(cond)->_cond, NULL)
#define omx__cond_destroy(cond) pthread_cond_destroy(&(cond)->_cond)
#define omx__cond_signal(cond) pthread_cond_signal(&(cond)->_cond)
#define omx__cond_wait(cond, lock) pthread_cond_wait(&(cond)->_cond, &(lock)->_mutex)

#else /* !OMX_LIB_THREAD_SAFETY */

struct omx__lock { /* nothing */ };
struct omx__cond { /* nothing */ };

#define omx__lock_init(lock) do { /* nothing */ } while (0)
#define omx__lock_destroy(lock) do { /* nothing */ } while (0)
#define omx__lock(lock) do { /* nothing */ } while (0)
#define omx__unlock(lock) do { /* nothing */ } while (0)

#define omx__cond_init(cond) do { /* nothing */ } while (0)
#define omx__cond_destroy(cond) do { /* nothing */ } while (0)
#define omx__cond_signal(cond) do { /* nothing */ } while (0)
#define omx__cond_wait(cond, lock) do { /* nothing */ } while (0)

#endif /* !OMX_LIB_THREAD_SAFETY */

#endif /* __omx_threads__ */
