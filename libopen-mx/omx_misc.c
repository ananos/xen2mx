/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
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
#include <errno.h>
#include <assert.h>

#include "omx_lib.h"

/***********************
 * Management of errors
 */

omx_return_t
omx__errno_to_return(char * caller)
{
  switch (errno) {
  case EINVAL:
    return OMX_INVALID_PARAMETER;
  case EACCES:
  case EPERM:
    return OMX_ACCESS_DENIED;
  case EMFILE:
  case ENFILE:
  case ENOMEM:
    return OMX_NO_SYSTEM_RESOURCES;
  case ENODEV:
  case ENOENT:
    return OMX_NO_DEVICE;
  case EBUSY:
    return OMX_BUSY;
  default:
    fprintf(stderr, "Open-MX: %s got unexpected errno %d (%m)\n",
	    caller, errno);
    return OMX_BAD_ERROR;
  }
}

const char *
omx_strerror(omx_return_t ret)
{
  switch (ret) {
  case OMX_SUCCESS:
    return "Success";
  case OMX_BAD_ERROR:
    return "Bad (internal?) error";
  case OMX_ALREADY_INITIALIZED:
    return "Already initialized";
  case OMX_NOT_INITIALIZED:
    return "Not initialized";
  case OMX_NO_DEVICE:
    return "No device";
  case OMX_ACCESS_DENIED:
    return "Access denied";
  case OMX_NO_RESOURCES:
    return "No resources available";
  case OMX_NO_SYSTEM_RESOURCES:
    return "No resources available in the system";
  case OMX_INVALID_PARAMETER:
    return "Invalid parameter";
  case OMX_NOT_IMPLEMENTED:
    return "Not implemented";
  case OMX_BAD_CONNECTION_KEY:
    return "Bad Connection Key";
  case OMX_BUSY:
    return "Resource Busy";
  case OMX_BAD_MATCH_MASK:
    return "Bad match mask.";
  case OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK:
    return "Matching info does not respect context id mask";
  case OMX_NOT_SUPPORTED_WITH_CONTEXT_ID:
    return "Feature not supported when context id are enabled";
  }
  assert(0);
}

const char *
omx_strstatus(omx_status_code_t code)
{
  switch (code) {
  case OMX_STATUS_SUCCESS:
    return "Success";
  case OMX_STATUS_FAILED:
    return "Failed";
  case OMX_STATUS_BAD_KEY:
    return "Bad Connection Key";
  }
  assert(0);
}
