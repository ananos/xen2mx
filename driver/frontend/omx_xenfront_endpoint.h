/*
 * Xen2MX
 * Copyright © Anastassios Nanos 2012
 * (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#ifndef __omx_xenfront_endpoint_h__
#define __omx_xenfront_endpoint_h__

#include "omx_xen.h"
#include "omx_xenfront.h"

int omx_ioctl_xen_open_endpoint(struct omx_endpoint *endpoint,
				void __user * uparam);
int omx_ioctl_xen_close_endpoint(struct omx_endpoint *endpoint,
				 void __user * uparam);

int omx_xen_endpoint_alloc_resources(struct omx_endpoint *endpoint);
void omx_xen_endpoint_free_resources(struct omx_endpoint *endpoint);

#endif				/* __omx_xenfront_endpoint_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
