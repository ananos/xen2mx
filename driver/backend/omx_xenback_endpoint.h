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

#ifndef __omx_xenback_endpoint_h__
#define __omx_xenback_endpoint_h__

#include <stdarg.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>

#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/xenbus.h>

#include "omx_xen.h"
#include "omx_xenback.h"

int omx_xen_endpoint_open(struct backend_info *be,
			  struct omx_xenif_request *req);
int omx_xen_endpoint_close(struct backend_info *be,
			   struct omx_xenif_request *req);

void omx_endpoint_destroy_workfunc(omx_work_struct_data_t data);

#endif				/* __omx_xenback_endpoint_h__ */
/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
