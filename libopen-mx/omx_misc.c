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
#include <errno.h>

#include "omx_lib.h"
#include "omx_request.h"

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
    omx__abort("%s got unexpected errno %d (%m)\n",
	       caller, errno);
  }
}

const char *
omx__strreqtype(enum omx__request_type type)
{
  switch (type) {
  case OMX_REQUEST_TYPE_CONNECT:
    return "Connect";
  case OMX_REQUEST_TYPE_SEND_TINY:
    return "Send Tiny";
  case OMX_REQUEST_TYPE_SEND_SMALL:
    return "Send Small";
  case OMX_REQUEST_TYPE_SEND_MEDIUM:
    return "Send Medium";
  case OMX_REQUEST_TYPE_SEND_LARGE:
    return "Send Large";
  case OMX_REQUEST_TYPE_RECV:
    return "Receive";
  case OMX_REQUEST_TYPE_RECV_LARGE:
    return "Receive Large";
  case OMX_REQUEST_TYPE_SEND_SELF:
    return "Send Self";
  case OMX_REQUEST_TYPE_RECV_SELF_UNEXPECTED:
    return "Receive Self Unexpected";
  default:
    omx__abort("unknown request type %d\n", (unsigned) type);
  }
}

/* API omx_strerror */
const char *
omx_strerror(omx_return_t ret)
{
  switch (ret) {
  case OMX_SUCCESS:
    return "Success";
  case OMX_BAD_ERROR:
    return "Bad error";
  case OMX_ALREADY_INITIALIZED:
    return "Already initialized";
  case OMX_NOT_INITIALIZED:
    return "Not initialized";
  case OMX_NO_DEVICE:
    return "No device";
  case OMX_ACCESS_DENIED:
    return "Access denied";
  case OMX_BOARD_NOT_FOUND:
    return "Board Not Found";
  case OMX_SEGMENTS_BAD_COUNT:
    return "Multiple Segments Count Invalid";
  case OMX_BAD_MATCH_MASK:
    return "Bad match mask.";
  case OMX_NO_RESOURCES:
    return "No resources available";
  case OMX_BUSY:
    return "Resource Busy";
  case OMX_BAD_INFO_KEY:
    return "Bad Info Key";
  case OMX_BAD_INFO_ADDRESS:
    return "Bad Info Value Address";
  case OMX_ENDPOINT_PARAMS_BAD_LIST:
    return "Bad Endpoint Parameter List";
  case OMX_ENDPOINT_PARAM_BAD_KEY:
    return "Bad Endpoint Parameter Key";
  case OMX_ENDPOINT_PARAM_BAD_VALUE:
    return "Bad Endpoint Parameter Value";
  case OMX_PEER_NOT_FOUND:
    return "Peer Not Found in the Table";
  case OMX_TIMEOUT:
    return "Command Timeout";
  case OMX_REMOTE_ENDPOINT_BAD_ID:
    return "Remote Endpoint Id is Wrong";
  case OMX_REMOTE_ENDPOINT_CLOSED:
    return "Remote Endpoint is Closed";
  case OMX_REMOTE_ENDPOINT_BAD_CONNECTION_KEY:
    return "Connection Key to Remote Endpoint is Invalid";
  case OMX_BAD_INFO_LENGTH:
    return "Bad Info Value Length";
  case OMX_NIC_ID_NOT_FOUND:
    return "Nic ID not Found in Peer Table";
  case OMX_BAD_KERNEL_ABI:
    return "Kernel ABI too old, did you rebuild/reload the new driver?";
  case OMX_BAD_LIB_ABI:
    return "Library ABI too old, did you relink your program with the new library?";
  case OMX_CANCEL_NOT_SUPPORTED:
    return "Cancel not supported for this request";
  case OMX_BAD_MATCHING_FOR_CONTEXT_ID_MASK:
    return "Matching info does not respect context id mask";
  case OMX_NOT_SUPPORTED_WITH_CONTEXT_ID:
    return "Operation not supported when context id are enabled";
  case OMX_REMOTE_RDMA_WINDOW_BAD_ID:
    return "Remote Window Id is Invalid";
  case OMX_REMOTE_ENDPOINT_UNREACHABLE:
    return "Remote Endpoint Unreachable";
  case OMX_REMOTE_ENDPOINT_BAD_SESSION:
    return "Wrong Remote Endpoint Session";
  case OMX_MESSAGE_ABORTED:
    return "Message Aborted";
  case OMX_MESSAGE_TRUNCATED:
    return "Message Truncated";
  case OMX_NOT_SUPPORTED_IN_HANDLER:
    return "Operation not supported in the handler";
  case OMX_NO_SYSTEM_RESOURCES:
    return "No resources available in the system";
  case OMX_INVALID_PARAMETER:
    return "Invalid parameter";
  case OMX_NOT_IMPLEMENTED:
    return "Not implemented";
  case OMX_RETURN_CODE_MAX:
    return "Maximum return code";
  }
  omx__abort("Failed to stringify unknown return value %d\n",
	     ret);
}

/*************************
 * Management of requests
 */

/* API omx_context */
omx_return_t
omx_context(omx_request_t *request, void ** context)
{
  *context = (*request)->generic.status.context;
  return OMX_SUCCESS;
}

/* API omx_cancel */
omx_return_t
omx_cancel(omx_endpoint_t ep,
	   omx_request_t *request,
	   uint32_t *result)
{
  union omx_request * req = *request;
  omx_return_t ret;

  OMX__ENDPOINT_LOCK(ep);

  ret = omx__progress(ep);
  if (ret != OMX_SUCCESS)
    goto out_with_lock;

  /* Search in the send request queue and recv request queue. */

  switch (req->generic.type) {
  case OMX_REQUEST_TYPE_RECV: {
    if (req->generic.state & OMX_REQUEST_STATE_RECV_NEED_MATCHING) {
      /* not matched, still in the recv queue */
      uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->recv.match_info);
      omx__dequeue_request(&ep->ctxid[ctxid].recv_req_q, req);
      omx__request_free(ep, req);
      *request = 0;
      *result = 1;
    } else {
      /* already matched, too late */
      *result = 0;
    }
    break;
  }

  case OMX_REQUEST_TYPE_RECV_LARGE:
    /* RECV are converted to RECV_LARGE when matched, so it's already too late */
    *result = 0;
    break;

  case OMX_REQUEST_TYPE_CONNECT:

    if (req->generic.state) {
      /* the request is pending on a queue */
      struct list_head * head = &ep->connect_req_q;
      omx__dequeue_request(head, req);
      omx__request_free(ep, req);
      *request = 0;
      *result = 1;
    } else {
      /* the request is already completed */
      *result = 0;
    }
    break;

  default:
    /* SEND_* are NOT cancellable with omx_cancel() */
    ret = omx__error_with_ep(ep, OMX_CANCEL_NOT_SUPPORTED,
			     "Cancelling %s request", omx__strreqtype(req->generic.type));
  }

 out_with_lock:
  OMX__ENDPOINT_UNLOCK(ep);
  return ret;
}
