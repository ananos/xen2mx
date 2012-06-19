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

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"
#include "omx_xen.h"
#include "omx_xenback_helper.h"
#include "omx_xenback.h"
#include "omx_endpoint.h"

static int omx_xenback_probe(struct xenbus_device *dev,
			     const struct xenbus_device_id *id)
{
	int ret = 0;
	struct backend_info *be;

	dprintk_in();

	ret = omx_xenback_allocate_basic_structures(dev, id);
	if (ret < 0) {
		xenbus_dev_fatal(dev, ret, "allocating backend and xenif");
		goto out;
	}

	be = dev_get_drvdata(&dev->dev);

	ret = omx_xenback_setup_evtchn(dev, be);
	if (ret < 0) {
		xenbus_dev_fatal(dev, ret, "setup event channel");
		goto out;
	}

	ret = omx_xenback_setup_xenbus(dev, be);
	if (ret) {
		printk_err("XenBus Setup failed\n");
		goto out;
	}

	ret = xenbus_switch_state(dev, XenbusStateInitialised);
	if (ret) {
		printk_err("XenBus switch state to Initialised failed\n");
		goto out;
	}

out:
	dprintk_out();
	return ret;
}

static int omx_xenback_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	dprintk_in();

	if (be->omx_xenif) {
		kobject_uevent(&dev->dev.kobj, KOBJ_OFFLINE);
		omx_xenif_disconnect(be->omx_xenif);
		be->omx_xenif = NULL;
	}

	if (be) {
		kfree(be);
	}

	dev_set_drvdata(&dev->dev, NULL);

	dprintk_out();
	return 0;

	return 0;
}

static int omx_xenback_uevent(struct xenbus_device *xdev,
			      struct kobj_uevent_env *env)
{
	dprintk_in();

	dprintk_out();
	return 0;
}

static void omx_xenback_frontend_changed(struct xenbus_device *dev,
					 enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	dprintk_in();
	dprintk_deb("frontend state = %s", xenbus_strstate(frontend_state));

	be->frontend_state = frontend_state;

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			dprintk_deb(KERN_INFO "%s: %s: prepare for reconnect\n",
				    __FUNCTION__, dev->nodename);
			xenbus_switch_state(dev, XenbusStateInitWait);
		}
		break;

	case XenbusStateInitialised:
		connect_ring(be);
		break;
	case XenbusStateConnected:
		if (dev->state == XenbusStateConnected)
			break;
		backend_create_omx(be);
		break;

	case XenbusStateClosing:
		if (be->omxdev) {
			int i;
			for (i = 0; i < OMX_XEN_MAX_ENDPOINTS; i++)
				kfree(be->omxdev->endpoints[i]);
			kfree(be->omxdev);
		}
#if 0
		if (be->omx_xenif) {
			kobject_uevent(&dev->dev.kobj, KOBJ_OFFLINE);
			omx_xenif_disconnect(be->omx_xenif);
			be->omx_xenif = NULL;
		}
#endif
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xenbus_switch_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
	dprintk_out();

	return;
}

static struct xenbus_device_id omx_xenback_ids[] = {
	{"omx"},
	{""}
};

static DEFINE_XENBUS_DRIVER(omx_xenback,,.probe = omx_xenback_probe,.remove =
			    omx_xenback_remove,.uevent =
			    omx_xenback_uevent,.otherend_changed =
			    omx_xenback_frontend_changed,);

int omx_xenback_init(void)
{
	int ret = 0;
	dprintk_in();

	if (!xen_domain() || !xen_initial_domain()) {
		ret = -ENODEV;
		printk_err
		    ("We are not running under Xen, or this "
		     "is *not* a privileged domain\n");
		goto out;
	}

	ret = xenbus_register_backend(&omx_xenback_driver);
	if (ret < 0)
		goto out;

out:
	dprintk_out();
	return ret;
}

void omx_xenback_exit(void)
{
	xenbus_unregister_driver(&omx_xenback_driver);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
