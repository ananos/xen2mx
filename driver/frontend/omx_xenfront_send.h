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

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/if_arp.h>
#include <linux/rcupdate.h>
#include <linux/ethtool.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pci.h>
#ifdef OMX_HAVE_MUTEX
#include <linux/mutex.h>
#endif

#include <stdarg.h>
#include <xen/interface/io/xenbus.h>
#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/page.h>

#include "omx_common.h"
#include "omx_reg.h"
#include "omx_endpoint.h"

#include "omx_xen.h"
#include "omx_xenfront.h"

int omx_ioctl_xen_send_notify(struct omx_endpoint *endpoint,
			      void __user * uparam);
int omx_ioctl_xen_send_connect_request(struct omx_endpoint *endpoint,
				       void __user * uparam);
int omx_ioctl_xen_send_connect_reply(struct omx_endpoint *endpoint,
				     void __user * uparam);
int omx_ioctl_xen_send_liback(struct omx_endpoint *endpoint,
			      void __user * uparam);
int omx_ioctl_xen_send_rndv(struct omx_endpoint *endpoint,
			    void __user * uparam);
int omx_ioctl_xen_pull(struct omx_endpoint *endpoint, void __user * uparam);
int omx_ioctl_xen_send_tiny(struct omx_endpoint *endpoint,
			    void __user * uparam);
int omx_ioctl_xen_send_small(struct omx_endpoint *endpoint,
			     void __user * uparam);
int omx_ioctl_xen_send_mediumva(struct omx_endpoint *endpoint,
				void __user * uparam);
int omx_ioctl_xen_send_mediumsq_frag(struct omx_endpoint *endpoint,
			     void __user * uparam);

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
