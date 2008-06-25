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
 * This file mimics mx_extensions.h from Myricom's MX distribution.
 * It is used to build applications on top of Open-MX using the MX ABI.
 */

#ifndef MX_EXTENSIONS_H
#define MX_EXTENSIONS_H

#include "myriexpress.h"

#define MX_HAS_ICONNECT_V2 1

extern mx_return_t mx_iconnect(mx_endpoint_t ep, uint64_t nic_id, uint32_t eid, uint32_t key,
			       uint64_t match_info, void *context, mx_request_t *request);

extern mx_return_t mx_disconnect(mx_endpoint_t ep, mx_endpoint_addr_t addr);

enum mx_unexp_handler_action {
  MX_RECV_CONTINUE = 0,
  MX_RECV_FINISHED
};

typedef enum mx_unexp_handler_action mx_unexp_handler_action_t;

typedef mx_unexp_handler_action_t
(*mx_unexp_handler_t)(void *context, mx_endpoint_addr_t source,
		      uint64_t match_value, uint32_t length,
		      void * data_if_available);

extern mx_return_t mx_register_unexp_handler(mx_endpoint_t ep, mx_unexp_handler_t handler, void *context);

extern mx_return_t mx_forget(mx_endpoint_t endpoint, mx_request_t *request);

extern mx_return_t mx_progress(mx_endpoint_t ep);

extern mx_return_t mx_set_endpoint_addr_context(mx_endpoint_addr_t endpoint_addr, void *context);
extern mx_return_t mx_get_endpoint_addr_context(mx_endpoint_addr_t endpoint_addr, void **context);

extern mx_return_t mx_set_request_timeout(mx_endpoint_t endpoint, mx_request_t request, uint32_t milli_seconds);

extern mx_return_t mx_decompose_endpoint_addr2(mx_endpoint_addr_t endpoint_addr, uint64_t *nic_id, uint32_t *endpoint_id, uint32_t *session_id);

#endif /* MX_EXTENSIONS_H */
