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

/*
 * This file provides API compatibility wrappers for building
 * native MX applications over Open-MX.
 */

#ifndef MX_EXTENSIONS_H
#define MX_EXTENSIONS_H

#include "open-mx.h"

#define MX_HAS_ICONNECT_V2 1
#define mx_iconnect omx_iconnect
#define mx_disconnect(...) MX_FAILURE

#define mx_set_request_timeout omx_set_request_timeout

#define MX_RECV_CONTINUE OMX_RECV_CONTINUE
#define MX_RECV_FINISHED OMX_RECV_FINISHED
typedef omx_unexp_handler_action_t mx_unexp_handler_action_t;
typedef omx_unexp_handler_t mx_unexp_handler_t;
#define mx_register_unexp_handler omx_register_unexp_handler

/* FIXME: mx_forget */

#define mx_progress omx_progress

#define mx_set_endpoint_addr_context omx_set_endpoint_addr_context
#define mx_get_endpoint_addr_context omx_get_endpoint_addr_context

#endif /* MX_EXTENSIONS_H */
