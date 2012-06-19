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

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_event.h"

int omx_xen_page_alloc(omx_xenif_t * omx_xenif, uint32_t count)
{
	struct omx_xen_page_cookie *cookie;
	struct page *page;
	int err = 0, i;

#ifdef OMX_XEN_COOKIES
	dprintk_in();

	for (i = 0; i < count; i++) {

		cookie =
		    kmalloc(sizeof(struct omx_xen_page_cookie), GFP_KERNEL);
		if (!cookie) {
			printk_err("cannot create cookie\n");
			err = -ENOMEM;
			goto out;
		}
		page = alloc_page(GFP_KERNEL);
		if (!page) {
			printk_err("cannot allocate page\n");
			err = -ENOMEM;
			goto out;
		}

		cookie->page = page;

		//      write_lock(&omx_xenif->page_cookies_freelock);
		list_add_tail(&cookie->node, &omx_xenif->page_cookies_free);
		//      write_unlock(&omx_xenif->page_cookies_freelock);

		dprintk_deb
		    ("allocated, and appended to list, %#lx, page = %#lx\n",
		     (unsigned long)cookie, (unsigned long)page);

	}

out:
	dprintk_out();
#endif
	return err;
}

void omx_xen_page_put_cookie(omx_xenif_t * omx_xenif,
			     struct omx_xen_page_cookie *cookie)
{
	dprintk_in();
#ifdef OMX_XEN_COOKIES
	//write_lock(&omx_xenif->page_cookies_freelock);
	list_move_tail(&cookie->node, &omx_xenif->page_cookies_free);
	//write_unlock(&omx_xenif->page_cookies_freelock);
#endif
	dprintk_out();
}

struct omx_xen_page_cookie *omx_xen_page_get_cookie(omx_xenif_t * omx_xenif)
{
	struct omx_xen_page_cookie *cookie;

	dprintk_in();

#ifdef OMX_XEN_COOKIES
	dprintk_deb("want an event cookie!\n");

	if ((volatile int)(list_empty(&omx_xenif->page_cookies_free))) {
		omx_xen_page_alloc(omx_xenif, 20);
	}
	// write_lock(&omx_xenif->page_cookies_freelock);

	cookie = list_first_entry(&omx_xenif->page_cookies_free,
				  struct omx_xen_page_cookie, node);
	if (!cookie) {
		printk_err("Error\n");
		goto out;
	}

	list_move_tail(&cookie->node, &omx_xenif->page_cookies_inuse);
	//  write_unlock(&omx_xenif->page_cookies_freelock);

	dprintk_deb("got it, %#010lx\n", (unsigned long)cookie);

out:
#endif
	dprintk_out();
	return cookie;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
