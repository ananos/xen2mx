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
#include <xen/balloon.h>
#include "omx_reg.h"

#define OMX_XEN_MAX_COOKIES 1024

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_event.h"

int omx_xen_page_alloc(omx_xenif_t * omx_xenif, uint32_t count)
{
	struct omx_xen_page_cookie *cookie;
	struct page **pages;
	int err = 0, i;

#ifdef OMX_XEN_COOKIES
	dprintk_in();

	cookie = kmalloc(sizeof(struct omx_xen_page_cookie), GFP_ATOMIC);
	if (!cookie) {
		printk_err("cannot create cookie\n");
		err = -ENOMEM;
		goto out;
	}
	pages = kmalloc(sizeof(struct page *) * count, GFP_ATOMIC);
	if (!pages) {
		printk_err("cannot malloc page list\n");
		err = -ENOMEM;
		goto out;
	}
	err = alloc_xenballooned_pages(count, pages, false);
	if (!pages || err) {
		printk_err("Error allocated xenballooned page\n");
		//err = -ENOMEM;
		goto out;
	}

	cookie->count = count;
	cookie->pages = pages;

	//      write_lock(&omx_xenif->page_cookies_freelock);
	list_add_tail(&cookie->node, &omx_xenif->page_cookies_free);
	//      write_unlock(&omx_xenif->page_cookies_freelock);

	dprintk_deb
	    ("allocated, and appended to list, %#lx, page = %#lx, count = %u\n",
	     (unsigned long)cookie, (unsigned long)pages, count);

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
	dprintk_deb("put it %#010lx\n", (unsigned long)cookie);
	//write_lock(&omx_xenif->page_cookies_freelock);
	list_move_tail(&cookie->node, &omx_xenif->page_cookies_free);
	//write_unlock(&omx_xenif->page_cookies_freelock);
#endif
	dprintk_out();
}

struct omx_xen_page_cookie *omx_xen_page_get_cookie(omx_xenif_t * omx_xenif,
						    uint32_t count)
{
	struct omx_xen_page_cookie *cookie;
	struct omx_xen_page_cookie *_cookie;
	struct omx_xen_page_cookie *toreturn = NULL;
	int i = 0;

	dprintk_in();

#ifdef OMX_XEN_COOKIES
	dprintk_deb("want an event cookie!\n");

	if ((volatile int)(list_empty(&omx_xenif->page_cookies_free))) {
		omx_xen_page_alloc(omx_xenif, count);
	}
	//write_lock(&omx_xenif->page_cookies_freelock);

again:

	cookie = NULL;
	_cookie = NULL;
	list_for_each_entry_safe(cookie, _cookie, &omx_xenif->page_cookies_free,
				 node) {
		if (cookie)
			dprintk_deb("cookie = %p, count = %d\n", cookie,
				    cookie->count);
		if (count <= cookie->count) {
			dprintk_deb
			    ("counts  match! %u, %u, cookie = %p\n",
			     cookie->count, count, (void *)cookie);
			list_move_tail(&cookie->node,
				       &omx_xenif->page_cookies_inuse);
			toreturn = cookie;
			break;
		}
	}

	if (!toreturn) {
		dprintk_deb("count not found, allocating one ;-) %u %u\n",
			    count, cookie->count);
		omx_xen_page_alloc(omx_xenif, count);
		/* FIXME: This is a fucking BUG!!! We add an error message to
		 * let us know about it! */
		if (i++ > OMX_XEN_MAX_COOKIES) {
			printk_err("Couldn't manage to find %d cookies, error\n", OMX_XEN_MAX_COOKIES);
			goto out;
		}
		goto again;
	}
	//list_move_tail(&cookie->node, &omx_xenif->page_cookies_inuse);
	//write_unlock(&omx_xenif->page_cookies_freelock);

	dprintk_deb("get it, %#010lx\n", (unsigned long)cookie);

out:
#endif
	dprintk_out();
	return toreturn;
}

int omx_xen_page_free_cookies(omx_xenif_t * omx_xenif)
{
	struct omx_xen_page_cookie *cookie;

	dprintk_in();

#ifdef OMX_XEN_COOKIES
	//dprintk_deb("want an event cookie!\n");

	while (!(list_empty(&omx_xenif->page_cookies_free))) {
		write_lock(&omx_xenif->page_cookies_freelock);

		cookie = list_first_entry(&omx_xenif->page_cookies_free,
					  struct omx_xen_page_cookie, node);
		if (!cookie) {
			printk_err("Error\n");
			goto out;
		}

		list_del(&cookie->node);
		write_unlock(&omx_xenif->page_cookies_freelock);
		/* If we add this one, we get slab corruption!!! */
		free_xenballooned_pages(cookie->count, cookie->pages);
		dprintk_deb("will free pages #010lx\n",
			    (unsigned long)cookie->pages);
		kfree(cookie->pages);
		dprintk_deb("will drop cookie #010lx\n", (unsigned long)cookie);
		kfree(cookie);
	}

out:
#endif
	dprintk_out();
	return 0;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
