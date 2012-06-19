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

#ifndef __omx_xenfront_h__
#define __omx_xenfront_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_xen.h"

int omx_ioctl_xen_user_region_create(struct omx_endpoint *endpoint,
				     void __user * uparam);
int omx_ioctl_xen_user_region_destroy(struct omx_endpoint *endpoint,
				      void __user * uparam);

int omx_poke_dom0(struct omx_xenfront_info *fe, int msg_id,
		  struct omx_xenif_request *ring_req);

int wait_for_backend_response(unsigned int *poll_var, unsigned int status);

/* FIXME: Do we really need this global var ? */



extern struct omx_xenfront_info *__omx_xen_frontend;

#endif				/* __omx_xenfront_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
