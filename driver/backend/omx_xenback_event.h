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

#ifndef __omx_xenback_page_cookie_h__
#define __omx_xenback_page_cookie_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_reg.h"

#include "omx_xen.h"
#include "omx_xenback.h"

struct omx_xen_page_cookie {
	struct list_head node;
	struct page *page;
};

int omx_xen_page_alloc(omx_xenif_t * omx_xenif, uint32_t count);
void omx_xen_page_put_cookie(omx_xenif_t * omx_xenif,
			     struct omx_xen_page_cookie *cookie);
struct omx_xen_page_cookie *omx_xen_page_get_cookie(omx_xenif_t * omx_xenif);

#endif				/* __omx_xenback_page_cookie_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
