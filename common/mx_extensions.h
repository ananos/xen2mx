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

/*
 * This file provides API compatibility wrappers for building
 * native MX applications over Open-MX.
 */

#ifndef MX_EXTENSIONS_H
#define MX_EXTENSIONS_H

#include "open-mx.h"

/***********************************
 * Redefine MX constants and macros
 */

#define MX_HAS_ICONNECT_V2 1

#define MX_RECV_CONTINUE OMX_UNEXP_HANDLER_RECV_CONTINUE
#define MX_RECV_FINISHED OMX_UNEXP_HANDLER_RECV_FINISHED

/********************
 * Redefine MX types
 */

typedef omx_unexp_handler_action_t mx_unexp_handler_action_t;
typedef omx_unexp_handler_t mx_unexp_handler_t;

/**********************************************************
 * MX API prototypes (needed for symbol-referenced compat)
 */

/******************************************
 * MX API wrappers (needed for API compat)
 */

#define mx_iconnect(ep,nic_id,eid,key,mi,ctx,req) omx_iconnect(ep,nic_id,eid,key,mi,ctx,req)
#define mx_disconnect(ep,addr) omx_disconnect(ep,addr)

#define mx_set_request_timeout(ep,req,ms) omx_set_request_timeout(ep,req,ms)

#define mx_register_unexp_handler(ep,hdlr,ctx) omx_register_unexp_handler(ep,hdlr,ctx)

#define mx_forget(ep,req) omx_forget(ep,req)

#define mx_progress(ep) omx_progress(ep)

#define mx_set_endpoint_addr_context(addr,ctx) omx_set_endpoint_addr_context(addr,ctx)
#define mx_get_endpoint_addr_context(addr,ctx) omx_get_endpoint_addr_context(addr,ctx)

#endif /* MX_EXTENSIONS_H */
