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
#include "omx_endpoint.h"

//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"

#include "omx_xen.h"
#include "omx_xenfront.h"
#include "omx_xenfront_helper.h"
#include "omx_xenfront_endpoint.h"

/* FIXME: Do we really need this global var ? */
struct omx_xenfront_info *__omx_xen_frontend;

static int omx_xenfront_probe(struct xenbus_device *dev,
			      const struct xenbus_device_id *id)
{
	struct omx_xenfront_info *fe;
	struct omx_xenif_sring *sring, *recv_sring;
	int err = 0;
	int i = 0;

	dprintk_in();

	dprintk_deb("Frontend Probe Fired!\n");
	fe = kzalloc(sizeof(*fe), GFP_KERNEL);
	dprintk_deb("fe info is @%#llx!\n", (unsigned long long)fe);
	if (!fe) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
		err = -ENOMEM;
		goto out;
	}
	__omx_xen_frontend = fe;

	for (i = 0; i < OMX_XEN_MAX_ENDPOINTS; i++) {
		fe->endpoints[i] = NULL;
	}

        fe->requests = kzalloc(OMX_MAX_INFLIGHT_REQUESTS * sizeof(enum frontend_status), GFP_KERNEL);

        spin_lock_init(&fe->status_lock);

	fe->xbdev = dev;
	fe->connected = OMXIF_STATE_DISCONNECTED;

        init_waitqueue_head(&fe->wq);
        fe->msg_workq =
            create_singlethread_workqueue("ReQ_FE");
        if (unlikely(!fe->msg_workq)) {
                printk_err("Couldn't create msg_workq!\n");
                err = -ENOMEM;
                goto out;
        }

        INIT_WORK(&fe->msg_workq_task, omx_xenif_interrupt);


	spin_lock_init(&fe->lock);
	dprintk_deb("Setting up shared ring\n");

	sring =
	    (struct omx_xenif_sring *)get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		err = -ENOMEM;
		goto out;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&fe->ring, sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(fe->ring.sring));
	if (err < 0) {
		free_page((unsigned long)sring);
		fe->ring.sring = NULL;
		printk_err("Failed to grant ring\n");
		goto out;
	}
	fe->ring_ref = err;


	recv_sring =
	    (struct omx_xenif_sring *)get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		err = -ENOMEM;
		goto out;
	}
	SHARED_RING_INIT(recv_sring);
	FRONT_RING_INIT(&fe->recv_ring, recv_sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(fe->recv_ring.sring));
	if (err < 0) {
		free_page((unsigned long)recv_sring);
		fe->recv_ring.sring = NULL;
		printk_err("Failed to grant recv_ring\n");
		goto out;
	}
	fe->recv_ring_ref = err;

	fe->handle = simple_strtoul(strrchr(dev->nodename, '/') + 1, NULL, 0);
	dprintk_deb("setting handle = %u\n", fe->handle);
	dev_set_drvdata(&dev->dev, fe);
	err = 0;
	//omx_xenfront_dev->info = info;
	//fe->endpoints = kzalloc(sizeof(struct omx_endpoint*) * OMX_XEN_MAX_ENDPOINTS, GFP_KERNEL);
	xenbus_switch_state(dev, XenbusStateInitialising);

out:
	dprintk_out();
	return err;

}

static int omx_xenfront_remove(struct xenbus_device *dev)
{
	struct omx_xenfront_info *fe = dev_get_drvdata(&dev->dev);

	dprintk_in();
	dprintk_deb("frontend_remove: %s removed\n", dev->nodename);
        /* This frees the page as a side-effect */
        if (fe->ring_ref)
                gnttab_end_foreign_access(fe->ring_ref, 0, (unsigned long)fe->ring.sring);

        /* This frees the page as a side-effect */
        if (fe->recv_ring_ref)
                gnttab_end_foreign_access(fe->recv_ring_ref, 0, (unsigned long)fe->recv_ring.sring);

	omx_xenif_free(fe, 0);

	xenbus_switch_state(fe->xbdev, XenbusStateClosing);
	dprintk_out();

	return 0;
}

static int omx_xenfront_uevent(struct xenbus_device *xdev,
			       struct kobj_uevent_env *env)
{
	dprintk_in();
	dprintk_out();

	return 0;
}

static void omx_xenfront_backend_changed(struct xenbus_device *dev,
					 enum xenbus_state backend_state)
{
	struct omx_xenfront_info *fe = dev_get_drvdata(&dev->dev);
	int ret = 0;

	dprintk_in();

	dprintk_deb("backend state %s\n", xenbus_strstate(backend_state));

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitWait:
		break;
	case XenbusStateInitialised:
		ret = talk_to_backend(dev, fe);
		if (ret) {
			printk_err("Error trying to talk to backend"
				   ", ret=%d\n", ret);
			//kfree(info);
		}
		break;
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
	case XenbusStateClosed:
		break;
	case XenbusStateConnected:
		if (dev->state == XenbusStateConnected)
			break;
		omx_xenfront_connect(fe);
		break;
	case XenbusStateClosing:
		dprintk_deb("Closing Xenbus\n");
		xenbus_frontend_closed(dev);
		break;
	}
	dprintk_out();

	return;
}

static struct xenbus_device_id omx_xenfront_ids[] = {
	{"omx"},
	{""}
};

static DEFINE_XENBUS_DRIVER(omx_xenfront,,.probe = omx_xenfront_probe,.remove =
			    omx_xenfront_remove,.uevent =
			    omx_xenfront_uevent,.otherend_changed =
			    omx_xenfront_backend_changed,);

int omx_xenfront_init(void)
{
	int ret = 0;
	dprintk_in();

	if (!xen_domain() || xen_initial_domain()) {
		ret = -ENODEV;
		printk_err
		    ("We are not running under Xen, or this "
		     "*is* a privileged domain\n");
		goto out;
	}

	ret = xenbus_register_frontend(&omx_xenfront_driver);
	if (ret) {
		printk_err("XenBus Registration Failed\n");
		goto out;
	}

	printk_inf("init\n");
out:
	dprintk_out();
	return ret;
}

void omx_xenfront_exit(void)
{
	/* Never succeed */
	if (xen_initial_domain())
		return;

	xenbus_unregister_driver(&omx_xenfront_driver);
	printk_inf("exit\n");
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
