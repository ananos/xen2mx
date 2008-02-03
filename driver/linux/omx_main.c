/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/rcupdate.h>

#include "omx_common.h"
#include "omx_peer.h"
#include "omx_endpoint.h"
#include "omx_region.h"
#include "omx_hal.h"

/********************
 * Module parameters
 */

static char * omx_ifnames = OMX_IFNAMES_DEFAULT;
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

int omx_skb_frags = MAX_SKB_FRAGS;
module_param_named(skbfrags, omx_skb_frags, uint, S_IRUGO);
MODULE_PARM_DESC(skbfrags, "Maximal number of fragments to attach to skb");

int omx_copybench = 0;
module_param_named(copybench, omx_copybench, uint, S_IRUGO);
MODULE_PARM_DESC(copybench, "Enable copy benchmark on startup");

#ifdef OMX_DRIVER_DEBUG
unsigned long omx_debug = 0;
module_param_named(debug, omx_debug, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Bitmask of debugging messages to display");

unsigned long omx_TINY_packet_loss = 0;
module_param_named(tiny_packet_loss, omx_TINY_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(tiny_packet_loss, "Explicit tiny reply packet loss frequency");

unsigned long omx_SMALL_packet_loss = 0;
module_param_named(small_packet_loss, omx_SMALL_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(small_packet_loss, "Explicit small reply packet loss frequency");

unsigned long omx_MEDIUM_FRAG_packet_loss = 0;
module_param_named(medium_frag_packet_loss, omx_MEDIUM_FRAG_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(medium_packet_loss, "Explicit medium reply packet loss frequency");

unsigned long omx_RNDV_packet_loss = 0;
module_param_named(rndv_packet_loss, omx_RNDV_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(rndv_packet_loss, "Explicit rndv reply packet loss frequency");

unsigned long omx_PULL_REQ_packet_loss = 0;
module_param_named(pull_packet_loss, omx_PULL_REQ_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(pull_packet_loss, "Explicit pull request packet loss frequency");

unsigned long omx_PULL_REPLY_packet_loss = 0;
module_param_named(pull_reply_packet_loss, omx_PULL_REPLY_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(pull_reply_packet_loss, "Explicit pull reply packet loss frequency");

unsigned long omx_NOTIFY_packet_loss = 0;
module_param_named(notify_packet_loss, omx_NOTIFY_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(notify_packet_loss, "Explicit notify packet loss frequency");

unsigned long omx_CONNECT_packet_loss = 0;
module_param_named(connect_packet_loss, omx_CONNECT_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(connect_packet_loss, "Explicit connect packet loss frequency");

unsigned long omx_TRUC_packet_loss = 0;
module_param_named(truc_packet_loss, omx_TRUC_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(truc_packet_loss, "Explicit truc packet loss frequency");

unsigned long omx_NACK_LIB_packet_loss = 0;
module_param_named(nack_lib_packet_loss, omx_NACK_LIB_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(nack_lib_packet_loss, "Explicit nack lib packet loss frequency");

unsigned long omx_NACK_MCP_packet_loss = 0;
module_param_named(nack_mcp_packet_loss, omx_NACK_MCP_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(nack_mcp_packet_loss, "Explicit nack mcp packet loss frequency");
#endif /* OMX_DRIVER_DEBUG */

/************************
 * Main Module Init/Exit
 */

#ifdef SVN_VERSION
#define VERSION PACKAGE_VERSION " (svn" SVN_VERSION ")"
#else
#define VERSION PACKAGE_VERSION
#endif

struct omx_driver_desc * omx_driver_userdesc = NULL; /* exported read-only to user-space */
static struct timer_list omx_driver_userdesc_update_timer;
static struct task_struct * omx_kthread_task = NULL;

static void
omx_driver_userdesc_update_handler(unsigned long data)
{
	omx_driver_userdesc->jiffies = jiffies;
	wmb();
	__mod_timer(&omx_driver_userdesc_update_timer, jiffies+1);
}

static int
omx_kthread_func(void *dummy)
{
	printk(KERN_INFO "Open-MX: kthread starting\n");

	while (1) {
		if (kthread_should_stop())
			break;

		msleep(1000);

		omx_endpoints_cleanup();
	}

	/*
	 * do a last round of cleanup before exiting since we might
	 * have been stopped before other resources were cleaned up
	 */
	omx_endpoints_cleanup();

	printk(KERN_INFO "Open-MX: kthread stopping\n");
	return 0;
}

static __init int
omx_init(void)
{
	int ret;

	printk(KERN_INFO "Open-MX " VERSION " initializing...\n");

	printk(KERN_INFO "Open-MX: configured for %d endpoints on %d interfaces with %d peers\n",
	       omx_endpoint_max, omx_iface_max, omx_peer_max);
	if (omx_endpoint_max > OMX_ENDPOINT_INDEX_MAX) {
		printk(KERN_INFO "Open-MX: Cannot use more than %d endpoints per board\n",
		       OMX_ENDPOINT_INDEX_MAX);
		ret = -EINVAL;
		goto out;
	}
	if (omx_peer_max > OMX_PEER_INDEX_MAX) {
		printk(KERN_INFO "Open-MX: Cannot use more than %d peers\n",
		       OMX_PEER_INDEX_MAX);
		ret = -EINVAL;
		goto out;
	}

	if (omx_skb_frags)
		printk(KERN_INFO "Open-MX: using at most %d frags per skb\n", omx_skb_frags);
	else
		printk(KERN_INFO "Open-MX: using linear skb only (no frags)\n");
	if (omx_skb_frags > MAX_SKB_FRAGS) {
		printk(KERN_INFO "Open-MX: Cannot use more than MAX_SKB_FRAGS (%ld) skb frags\n",
		       (unsigned long) MAX_SKB_FRAGS);
		ret = -EINVAL;
		goto out;
	}

	printk(KERN_INFO "Open-MX: using Ethertype 0x%lx\n",
	       (unsigned long) ETH_P_OMX);
	printk(KERN_INFO "Open-MX: requires MTU >= %ld\n",
	       (unsigned long) OMX_MTU_MIN);
	printk(KERN_INFO "Open-MX: using %ld x %ldkB pull replies per request\n",
	       (unsigned long) OMX_PULL_REPLY_PER_BLOCK,
	       (unsigned long) OMX_PULL_REPLY_LENGTH_MAX);

#ifdef OMX_DRIVER_DEBUG
	if (omx_TINY_packet_loss)
		printk(KERN_INFO "Open-MX: simulating tiny packet loss every %ld packets\n",
		       omx_TINY_packet_loss);
	if (omx_SMALL_packet_loss)
		printk(KERN_INFO "Open-MX: simulating small packet loss every %ld packets\n",
		       omx_SMALL_packet_loss);
	if (omx_MEDIUM_FRAG_packet_loss)
		printk(KERN_INFO "Open-MX: simulating medium frag packet loss every %ld packets\n",
		       omx_MEDIUM_FRAG_packet_loss);
	if (omx_RNDV_packet_loss)
		printk(KERN_INFO "Open-MX: simulating rndv packet loss every %ld packets\n",
		       omx_RNDV_packet_loss);
	if (omx_PULL_REQ_packet_loss)
		printk(KERN_INFO "Open-MX: simulating pull request packet loss every %ld packets\n",
		       omx_PULL_REQ_packet_loss);
	if (omx_PULL_REPLY_packet_loss)
		printk(KERN_INFO "Open-MX: simulating pull reply packet loss every %ld packets\n",
		       omx_PULL_REPLY_packet_loss);
	if (omx_NOTIFY_packet_loss)
		printk(KERN_INFO "Open-MX: simulating notify packet loss every %ld packets\n",
		       omx_NOTIFY_packet_loss);
	if (omx_CONNECT_packet_loss)
		printk(KERN_INFO "Open-MX: simulating connect packet loss every %ld packets\n",
		       omx_CONNECT_packet_loss);
	if (omx_TRUC_packet_loss)
		printk(KERN_INFO "Open-MX: simulating truc packet loss every %ld packets\n",
		       omx_TRUC_packet_loss);
	if (omx_NACK_LIB_packet_loss)
		printk(KERN_INFO "Open-MX: simulating nack lib packet loss every %ld packets\n",
		       omx_NACK_LIB_packet_loss);
	if (omx_NACK_MCP_packet_loss)
		printk(KERN_INFO "Open-MX: simulating nack mcp packet loss every %ld packets\n",
		       omx_NACK_MCP_packet_loss);
#endif /* OMX_DRIVER_DEBUG */

	omx_driver_userdesc = omx_vmalloc_user(sizeof(struct omx_driver_desc));
	if (!omx_driver_userdesc) {
		printk(KERN_ERR "Open-MX: failed to allocate driver user descriptor\n");
		ret = -ENOMEM;
		goto out;
	}

	/* fill the driver descriptor */
	omx_driver_userdesc->abi_version = OMX_DRIVER_ABI_VERSION;
	omx_driver_userdesc->board_max = omx_iface_max;
	omx_driver_userdesc->endpoint_max = omx_endpoint_max;
	omx_driver_userdesc->peer_max = omx_peer_max;
	omx_driver_userdesc->hz = HZ;
	omx_driver_userdesc->jiffies = jiffies;

	/* setup a timer to update jiffies in the driver user descriptor */
	setup_timer(&omx_driver_userdesc_update_timer, omx_driver_userdesc_update_handler, 0);
	__mod_timer(&omx_driver_userdesc_update_timer, jiffies+1);

	ret = omx_dma_init();
	if (ret < 0)
		goto out_with_driver_userdesc;


	ret = omx_peers_init();
	if (ret < 0)
		goto out_with_dma;

	ret = omx_net_init((const char *) omx_ifnames);
	if (ret < 0)
		goto out_with_peers;

	omx_kthread_task = kthread_run(omx_kthread_func, NULL, "open-mxd");
	if (IS_ERR(omx_kthread_func)) {
		ret = PTR_ERR(omx_kthread_task);
		goto out_with_net;
	}

	ret = omx_dev_init();
	if (ret < 0)
		goto out_with_kthread;

	printk(KERN_INFO "Open-MX initialized\n");
	return 0;

 out_with_kthread:
	kthread_stop(omx_kthread_task);
 out_with_net:
	omx_net_exit();
 out_with_peers:
	omx_peers_init();
 out_with_dma:
	omx_dma_exit();
 out_with_driver_userdesc:
	del_timer_sync(&omx_driver_userdesc_update_timer);
	vfree(omx_driver_userdesc);
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
	kthread_stop(omx_kthread_task);
	omx_net_exit();
	omx_peers_exit();
	omx_dma_exit();
	del_timer_sync(&omx_driver_userdesc_update_timer);
	vfree(omx_driver_userdesc);
	synchronize_rcu();
	printk(KERN_INFO "Open-MX " VERSION " terminated\n");
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
