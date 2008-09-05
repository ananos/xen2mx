/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "open-mx.h"
#include "omx_lib.h"

/****************************
 * Predefined error handlers
 */

static omx_return_t
omx__errors_before_init(const char *buffer, omx_return_t ret)
{
  omx__printf("BeforeInit: %s: %s\n", buffer, omx_strerror(ret));
  exit(-1);
}

omx_return_t
omx__errors_are_fatal(const char *buffer, omx_return_t ret)
{
  omx__printf("%s: %s\n", buffer, omx_strerror(ret));
  exit(-1);
}
const omx_error_handler_t OMX_ERRORS_ARE_FATAL = (omx_error_handler_t) omx__errors_are_fatal;

omx_return_t
omx__errors_return(const char *buffer, omx_return_t ret)
{
  return ret;
}
const omx_error_handler_t OMX_ERRORS_RETURN = (omx_error_handler_t) omx__errors_return;

/* the current handler is fatal by default, with a special one before init */
static omx_error_handler_t omx__error_handler = (omx_error_handler_t) omx__errors_before_init;

void
omx__init_error_handler(void)
{
  omx__error_handler = omx__globals.fatal_errors ?
			(omx_error_handler_t) omx__errors_are_fatal
			: (omx_error_handler_t) omx__errors_return;
}

/***********************************************************
 * Internal error callback to use in case of internal error
 */

#define BUFFER_MAX 256

omx_return_t
omx__error(omx_return_t ret, const char *fmt, ...)
{
  char buffer[BUFFER_MAX];
  va_list va;
  int err;

  if (ret == OMX_SUCCESS)
    return OMX_SUCCESS;

  va_start(va, fmt);
  err = vsnprintf(buffer, BUFFER_MAX, fmt, va);
  omx__debug_assert(err < BUFFER_MAX);
  va_end(va);

  return omx__error_handler(buffer, ret);
}

omx_return_t
omx__error_with_ep(struct omx_endpoint *ep,
		   omx_return_t ret, const char *fmt, ...)
{
  omx_error_handler_t handler;
  char buffer[BUFFER_MAX];
  va_list va;
  int err;

  if (ret == OMX_SUCCESS)
    return OMX_SUCCESS;

  va_start(va, fmt);
  err = vsnprintf(buffer, BUFFER_MAX, fmt, va);
  omx__debug_assert(err < BUFFER_MAX);
  va_end(va);

  handler = ep->error_handler ? : omx__error_handler;
  return handler(buffer, ret);
}

omx_return_t
omx__error_with_req(struct omx_endpoint *ep, union omx_request *req,
		    omx_return_t code, const char *fmt, ...)
{
  omx_error_handler_t handler;
  char buffer[BUFFER_MAX];
  va_list va;
  int err;

  if (code == OMX_SUCCESS)
    return OMX_SUCCESS;

  va_start(va, fmt);
  err = vsnprintf(buffer, BUFFER_MAX, fmt, va);
  omx__debug_assert(err < BUFFER_MAX);
  va_end(va);

  handler = ep->error_handler ? : omx__error_handler;
  return handler(buffer, code);
}

/************************
 * Change error handlers
 */

/* API omx_set_error_handler */
omx_error_handler_t
omx_set_error_handler(omx_endpoint_t ep, omx_error_handler_t new_handler)
{
  omx_error_handler_t old_handler;

  if (ep) {
    OMX__ENDPOINT_LOCK(ep);
    old_handler = ep->error_handler;
    ep->error_handler = new_handler;
    OMX__ENDPOINT_UNLOCK(ep);
  } else {
    old_handler = omx__error_handler;
    omx__error_handler = new_handler;
  }

  return old_handler;
}

