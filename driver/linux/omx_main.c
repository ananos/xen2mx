/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
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
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "omx_common.h"

/********************
 * Module parameters
 */

static char * omx_ifnames = NULL;
module_param_named(ifnames, omx_ifnames, charp, S_IRUGO); /* modifiable by the attached sysfs file */
MODULE_PARM_DESC(ifnames, "Interfaces to attach on startup");

int omx_iface_max = 32;
module_param_named(ifaces, omx_iface_max, uint, S_IRUGO);
MODULE_PARM_DESC(ifaces, "Maximum number of attached interfaces");

int omx_endpoint_max = 8;
module_param_named(endpoints, omx_endpoint_max, uint, S_IRUGO);
MODULE_PARM_DESC(endpoints, "Maximum number of endpoints per interface");

int omx_peer_max = 1024;
module_param_named(peers, omx_peer_max, uint, S_IRUGO);
MODULE_PARM_DESC(peers, "Maximum number of peer nodes");

int omx_copybench = 0;
module_param_named(copybench, omx_copybench, uint, S_IRUGO);
MODULE_PARM_DESC(copybench, "Enable copy benchmark on startup");

#ifdef OMX_DEBUG
unsigned long omx_debug = 0;
module_param_named(debug, omx_debug, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Bitmask of debugging messages to display");

unsigned long omx_pull_packet_loss = 0;
module_param_named(pull_packet_loss, omx_pull_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(pull_packet_loss, "Explicit pull reply packet loss frequency");
#endif

/************************
 * Main Module Init/Exit
 */

struct omx_driver_desc * driver_desc = NULL;

static __init int
omx_init(void)
{
	int ret;

	printk(KERN_INFO "Open-MX initializing...\n");
	printk(KERN_INFO "Open-MX: using Ethertype 0x%lx\n",
	       (unsigned long) ETH_P_OMX);
	printk(KERN_INFO "Open-MX: requires MTU >= %ld\n",
	       (unsigned long) OMX_MTU_MIN);
	printk(KERN_INFO "Open-MX: using %ld x %ldkB pull replies per request\n",
	       (unsigned long) OMX_PULL_REPLY_PER_BLOCK,
	       (unsigned long) OMX_PULL_REPLY_LENGTH_MAX);
#ifdef OMX_DEBUG
	if (omx_pull_packet_loss)
		printk(KERN_INFO "Open-MX: simulating pull reply packet loss every %ld packets\n",
		       omx_pull_packet_loss);
#endif

	driver_desc = vmalloc_user(sizeof(struct omx_driver_desc));
	if (!driver_desc) {
		printk(KERN_ERR "Open-MX: failed to allocate driver descriptor\n");
		ret = -ENOMEM;
		goto out;
	}
	driver_desc->hz = HZ;

	ret = omx_dma_init();
	if (ret < 0)
		goto out_with_driver_desc;

	ret = omx_peers_init();
	if (ret < 0)
		goto out_with_dma;

	ret = omx_net_init((const char *) omx_ifnames);
	if (ret < 0)
		goto out_with_peers;

	ret = omx_dev_init();
	if (ret < 0)
		goto out_with_net;

	printk(KERN_INFO "Open-MX initialized\n");
	return 0;

 out_with_net:
	omx_net_exit();
 out_with_peers:
	omx_peers_init();
 out_with_dma:
	omx_dma_exit();
 out_with_driver_desc:
	vfree(driver_desc);
 out:
	printk(KERN_ERR "Failed to initialize Open-MX\n");
	return ret;
}
module_init(omx_init);

static __exit void
omx_exit(void)
{
	printk(KERN_INFO "Open-MX terminating...\n");
	omx_dev_exit();
	omx_net_exit();
	omx_peers_init();
	omx_dma_exit();
	vfree(driver_desc);
	printk(KERN_INFO "Open-MX terminated\n");
}
module_exit(omx_exit);

/***************
 * Module Infos
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brice Goglin <Brice.Goglin@inria.fr>");
MODULE_VERSION(PACKAGE_VERSION);
MODULE_DESCRIPTION(PACKAGE_NAME ": Myrinet Express over generic Ethernet");

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
