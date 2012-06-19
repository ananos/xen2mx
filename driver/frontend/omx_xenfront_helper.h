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

#ifndef __omx_xenfront_helper_h__
#define __omx_xenfront_helper_h__

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
#include "omx_xenfront.h"
//#define EXTRA_DEBUG_OMX
#include "omx_xen_debug.h"

//irqreturn_t omx_xenif_interrupt(int irq, void *data);

irqreturn_t omx_xenif_fe_int(int irq, void *data)
{
	struct omx_xenfront_info *fe = (struct omx_xenfront_info *)data;
	unsigned long flags;


	dprintk_in();

	//spin_lock_irqsave(&fe->lock, flags);
	//queue_work(fe->msg_workq, &fe->msg_workq_task);
	/* dprintk_deb("ev_id %#lx omxbe=%#lx\n", (unsigned long)data, (unsigned long)fe); */
	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->recv_ring)) {
		omx_xenif_interrupt_recv(&fe->msg_workq_task);
	}
	if (RING_HAS_UNCONSUMED_RESPONSES(&fe->ring)) {
		omx_xenif_interrupt(&fe->msg_workq_task);
	}

	//spin_unlock_irqrestore(&fe->lock, flags);
	dprintk_out();
	return IRQ_HANDLED;
}

static void omx_xenif_free(struct omx_xenfront_info *fe, int suspend)
{
	dprintk_in();

	destroy_workqueue(fe->msg_workq);
	kfree(fe);

	dprintk_out();
	return;
}

static int setup_ring(struct xenbus_device *dev, struct omx_xenfront_info *fe)
{
	int err = 0;

	dprintk_in();
	// fe->ring_ref = 0;

	fe->evtchn.remote_dom = 0;	/* DOM0_ID */
	if ((err =
	     HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
					 &fe->evtchn))) {
		printk("failed to setup evtchn ! err = %d\n", err);
		goto out;
	}

	err = bind_evtchn_to_irqhandler(fe->evtchn.local_port,
					omx_xenif_fe_int, IRQF_SAMPLE_RANDOM,
					"domU", fe);

	if (err < 0) {
		dprintk_deb("failed to bind irqhandler! err = %d\n", err);
		goto out;
	}
	fe->irq = err;
	dprintk_deb
	    ("ring-ref = %u, fe->recv_ring_ref = %u, irq = %u, port = %u\n",
	     fe->ring_ref, fe->recv_ring_ref, fe->irq, fe->evtchn.remote_port);
	dprintk_out();
	return 0;
out:
	omx_xenif_free(fe, 0);
	dprintk_out();
	return err;
}

static void omx_xenfront_connect(struct omx_xenfront_info *fe)
{

	dprintk_in();
	if ((fe->connected == OMXIF_STATE_CONNECTED) ||
	    (fe->connected == OMXIF_STATE_SUSPENDED)) {
		dprintk_out();
		return;
	}

	xenbus_switch_state(fe->xbdev, XenbusStateConnected);

	//spin_lock_irq(&omxif_io_lock);
	fe->connected = OMXIF_STATE_CONNECTED;
	//spin_unlock_irq(&omxif_io_lock);
        INIT_LIST_HEAD(&fe->gref_cookies_free);
        rwlock_init(&fe->gref_cookies_freelock);
        INIT_LIST_HEAD(&fe->gref_cookies_inuse);
        rwlock_init(&fe->gref_cookies_inuselock);



	fe->is_ready = 1;
	dprintk_out();
}

static int talk_to_backend(struct xenbus_device *dev,
			   struct omx_xenfront_info *fe)
{
	const char *message = NULL;
	struct xenbus_transaction xbt;
	int err;

	dprintk_in();

	dprintk_inf("nodename is %s\n", dev->nodename);
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		printk_err("starting transaction failed\n");
		goto destroy_ring;
	}
#if 0
	err = xenbus_printf(xbt, dev->nodename, "handle", "%u", fe->handle);
	if (err) {
		message = "writing handle";
		goto abort_transaction;
	}
	dprintk_deb("xenbus handle written: %u\n", fe->handle);
#endif

	xenbus_scanf(XBT_NIL, dev->nodename, "port", "%d",
		     &fe->evtchn.remote_port);
	if (!(fe->evtchn.remote_port)) {
		printk_err("error, port = 0\n");
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "ring-ref", "%u", fe->ring_ref);
	if (err) {
		message = "writing ring-ref";
		goto abort_transaction;
	}
	err =
	    xenbus_printf(xbt, dev->nodename, "recv-ring-ref", "%u",
			  fe->recv_ring_ref);
	if (err) {
		message = "writing recv-ring-ref";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename,
			    "event-channel", "%u", fe->evtchn.local_port);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}
#if 0
	err = xenbus_printf(xbt, dev->nodename, "protocol", "%s",
			    XEN_IO_PROTO_ABI_NATIVE);
	if (err) {
		message = "writing protocol";
		goto abort_transaction;
	}
#endif

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		printk_err("completing transaction failed\n");
		goto destroy_ring;
	}

	err = setup_ring(dev, fe);
	xenbus_switch_state(dev, XenbusStateInitialised);
	if (err) {
		printk_err("error setup ring\n");
		goto out;
	}

	dprintk_out();
	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message) {
		xenbus_dev_fatal(dev, err, "%s", message);
		printk_err("%s\n", message);
	}
destroy_ring:
	omx_xenif_free(fe, 0);
out:
	dprintk_out();
	return err;
}

#endif				/* __omx_xenfront_helper_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
