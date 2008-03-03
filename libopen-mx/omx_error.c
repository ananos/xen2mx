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

omx_return_t
omx__errors_are_fatal(const char *buffer, omx_return_t ret)
{
  fprintf(stderr, "Open-MX: %s: %s\n", buffer, omx_strerror(ret));
  exit(-1);
}

omx_return_t
omx__errors_return(const char *buffer, omx_return_t ret)
{
  return ret;
}

/* the current handler is fatal by default */
static
omx_error_handler_t omx__error_handler = (omx_error_handler_t) omx__errors_are_fatal;

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
omx__error_with_req(struct omx_endpoint *ep, union omx_request *req,
		    omx_return_t code, const char *fmt, ...)
{
  char buffer[BUFFER_MAX];
  va_list va;
  int err;

  if (code == OMX_SUCCESS)
    return OMX_SUCCESS;

  va_start(va, fmt);
  err = vsnprintf(buffer, BUFFER_MAX, fmt, va);
  omx__debug_assert(err < BUFFER_MAX);
  va_end(va);

  return omx__error_handler(buffer, code);
}
