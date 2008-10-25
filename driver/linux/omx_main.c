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
#include "omx_wire.h"
#include "omx_peer.h"
#include "omx_iface.h"
#include "omx_endpoint.h"
#include "omx_reg.h"
#include "omx_dma.h"
#include "omx_hal.h"

/********************
 * Module parameters
 */

/* allow module parameters to be ignored when not supporting */

int /* not static since maybe unused (and __maybe_unused appeared in 2.6.23) */
omx_unavail_module_param_set(const char *buf, struct kernel_param *kp)
{
  printk(KERN_INFO "Open-MX: WARNING: %s\n", (char*) kp->arg);
  return 0;
}
#define omx_unavail_module_param(name, reason) \
module_param_call(name, omx_unavail_module_param_set, NULL, #name " unavailable unless " reason, S_IRUGO); \
MODULE_PARM_DESC(name, "Unavailable unless " reason)

/* actual module parameters */

module_param_call(ifnames, omx_ifnames_set, omx_ifnames_get, NULL, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ifnames, "Interfaces to attach on startup, comma-separated");

int omx_iface_max = 32;
module_param_named(ifaces, omx_iface_max, uint, S_IRUGO);
MODULE_PARM_DESC(ifaces, "Maximum number of attached interfaces");

int omx_endpoint_max = 32;
module_param_named(endpoints, omx_endpoint_max, uint, S_IRUGO);
MODULE_PARM_DESC(endpoints, "Maximum number of endpoints per interface");

int omx_peer_max = 1024;
module_param_named(peers, omx_peer_max, uint, S_IRUGO);
MODULE_PARM_DESC(peers, "Maximum number of peer nodes");

int omx_skb_frags = MAX_SKB_FRAGS;
module_param_named(skbfrags, omx_skb_frags, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(skbfrags, "Maximal number of fragments to attach to skb");

int omx_skb_copy_max = 0;
module_param_named(skbcopy, omx_skb_copy_max, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(skbcopy, "Maximum length of data to copy in linear skb instead of attaching pages");

int omx_region_demand_pin = 0;
module_param_named(demandpin, omx_region_demand_pin, uint, S_IRUGO); /* not writable to simplify things */
MODULE_PARM_DESC(demandpin, "Defer region pinning until really needed and use demand-pinning");

int omx_pin_chunk_pages_min = 1;
module_param_named(pinchunkmin, omx_pin_chunk_pages_min, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(pinchunkmin, "Minimum number of pages to pin at once");

int omx_pin_chunk_pages_max = 1024;
module_param_named(pinchunkmax, omx_pin_chunk_pages_max, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(pinchunkmax, "Maximum number of pages to pin at once");

int omx_pin_invalidate = 0;
module_param_named(pininvalidate, omx_pin_invalidate, uint, S_IRUGO);
MODULE_PARM_DESC(pininvalidate, "User region pin invalidating when MMU notifiers are supported");

#ifdef CONFIG_NET_DMA
int omx_dmaengine = 0; /* disabled by default for now */
module_param_call(dmaengine, omx_set_dmaengine, param_get_uint, &omx_dmaengine, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dmaengine, "Enable DMA engine support");
int omx_dma_async_frag_min = 1024;
module_param_named(dmaasyncfragmin, omx_dma_async_frag_min, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dmaasyncfragmin, "Minimum fragment length to offload asynchronous copy on DMA engine");
int omx_dma_async_min = 64*1024;
module_param_named(dmaasyncmin, omx_dma_async_min, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dmaasyncmin, "Minimum message length to offload all fragment copies asynchronously on DMA engine");
int omx_dma_sync_min = 2*1024*1024;
module_param_named(dmasyncmin, omx_dma_sync_min, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dmasyncmin, "Minimum length to offload synchronous copy on DMA engine");
#else /* CONFIG_NET_DMA */
omx_unavail_module_param(dmaengine, "kernel has CONFIG_NET_DMA");
omx_unavail_module_param(dmaasyncfragmin, "kernel has CONFIG_NET_DMA");
omx_unavail_module_param(dmaasyncmin, "kernel has CONFIG_NET_DMA");
omx_unavail_module_param(dmasyncmin, "kernel has CONFIG_NET_DMA");
#endif /* CONFIG_NET_DMA */

int omx_copybench = 0;
module_param_named(copybench, omx_copybench, uint, S_IRUGO);
MODULE_PARM_DESC(copybench, "Enable copy benchmark on startup");

#ifdef OMX_DRIVER_DEBUG
unsigned long omx_debug = 0;
module_param_named(debug, omx_debug, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Bitmask of debugging messages to display");
#else /* OMX_DRIVER_DEBUG */
omx_unavail_module_param(debug, "--enable-debug was given");
#endif /* OMX_DRIVER_DEBUG */

#ifdef OMX_DRIVER_DEBUG
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
unsigned long omx_RAW_packet_loss = 0;
module_param_named(raw_packet_loss, omx_RAW_packet_loss, ulong, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(raw_packet_loss, "Explicit raw packet loss frequency");
#else /* OMX_DRIVER_DEBUG */
omx_unavail_module_param(tiny_packet_loss, "--enable-debug was given");
omx_unavail_module_param(small_packet_loss, "--enable-debug was given");
omx_unavail_module_param(medium_frag_packet_loss, "--enable-debug was given");
omx_unavail_module_param(rndv_packet_loss, "--enable-debug was given");
omx_unavail_module_param(pull_packet_loss, "--enable-debug was given");
omx_unavail_module_param(pull_reply_packet_loss, "--enable-debug was given");
omx_unavail_module_param(notify_packet_loss, "--enable-debug was given");
omx_unavail_module_param(connect_packet_loss, "--enable-debug was given");
omx_unavail_module_param(truc_packet_loss, "--enable-debug was given");
omx_unavail_module_param(nack_lib_packet_loss, "--enable-debug was given");
omx_unavail_module_param(nack_mcp_packet_loss, "--enable-debug was given");
omx_unavail_module_param(raw_packet_loss, "--enable-debug was given");
#endif /* OMX_DRIVER_DEBUG */

/************************
 * Main Module Init/Exit
 */

#ifdef SRC_VERSION
#define VERSION PACKAGE_VERSION " (" SRC_VERSION ")"
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
		omx_user_regions_cleanup();
		omx_process_host_queries_and_replies();
		omx_process_peers_to_host_query();
	}

	/*
	 * do a last round of cleanup before exiting since we might
	 * have been stopped before other resources were cleaned up
	 */
	omx_endpoints_cleanup();
	omx_user_regions_cleanup();

	printk(KERN_INFO "Open-MX: kthread stopping\n");
	return 0;
}

char *
omx_get_driver_string(unsigned int *lenp)
{
	char *buffer, *tmp;
	unsigned int buflen, len;

	/* setup the driver desc string */
#define OMX_DRIVER_STRING_LEN 1024
	buffer = kmalloc(OMX_DRIVER_STRING_LEN, GFP_KERNEL);
	if (!buffer) {
		printk(KERN_ERR "Open-MX: failed to allocate driver string\n");
		return NULL;
	}
	tmp = buffer;
	buflen = 0;

	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		"Open-MX " VERSION "\n");
	tmp += len;
	buflen += len;

	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" Driver ABI=0x%lx\n",
		(unsigned long) omx_driver_userdesc->abi_version);
	tmp += len;
	buflen += len;

	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" Configured for %d endpoints on %d interfaces with %d peers\n",
		omx_endpoint_max, omx_iface_max, omx_peer_max);
	tmp += len;
	buflen += len;

	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		 " WireSpecs: %s EtherType=0x%lx MTU>=0x%ld\n",
		 omx_driver_userdesc->features & OMX_DRIVER_FEATURE_WIRECOMPAT ? "WireCompatible" : "NoWireCompat",
		 (unsigned long) ETH_P_OMX, (unsigned long) OMX_MTU);
	tmp += len;
	buflen += len;

	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		 " MediumMessages: %ldB per fragment\n",
		 (unsigned long) OMX_MEDIUM_FRAG_LENGTH_MAX);
	tmp += len;
	buflen += len;
	
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		 " LargeMessages: %ld requests in parallel, %ld x %ldB pull replies per request\n",
		 (unsigned long) OMX_PULL_BLOCK_DESCS_NR,
		 (unsigned long) OMX_PULL_REPLY_PER_BLOCK,
		 (unsigned long) OMX_PULL_REPLY_LENGTH_MAX);
	tmp += len;
	buflen += len;
	
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" SkBuff: <=%d frags%s, ForcedCopy <=%dB\n",
		omx_skb_frags, omx_skb_frags ? "" : " (always linear)", omx_skb_copy_max);
	tmp += len;
	buflen += len;

	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" SharedComms: %s\n",
		omx_driver_userdesc->features & OMX_DRIVER_FEATURE_SHARED ? "Enabled" : "Disabled");
	tmp += len;
	buflen += len;

	if (omx_region_demand_pin)
		len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
			" Pinning: OnDemand ChunkPagesMin=%ld Max=%ld\n",
			(unsigned long) omx_pin_chunk_pages_min,
			(unsigned long) omx_pin_chunk_pages_max);
	else
		len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
			" Pinning: Immediate\n");
	tmp += len;
	buflen += len;

#ifdef CONFIG_MMU_NOTIFIER
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" PinInvalidate: KernelSupported %s\n",
		omx_driver_userdesc->features & OMX_DRIVER_FEATURE_PIN_INVALIDATE ? "Enabled" : "Disabled");
#else
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" PinInvalidate: NoKernelSupport\n");
#endif
	tmp += len;
	buflen += len;

#ifdef CONFIG_NET_DMA
	if (omx_dmaengine)
		len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
			" DMAEngine: KernelSupported Enabled SyncCopyMin=%dB AsyncCopyMin=%dB (%dB per packet)\n",
			omx_dma_sync_min, omx_dma_async_min, omx_dma_async_frag_min);
	else
		len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
			" DMAEngine: KernelSupported Disabled\n");
#else
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" DMAEngine: NoKernelSupport\n");
#endif
	tmp += len;
	buflen += len;

#ifdef OMX_DRIVER_DEBUG
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" Debug: Enabled MessageMask=0x%lx\n"
		" DebugLossPacket: Tiny=%ld Small=%ld MediumFrag=%ld Rndv=%ld Pull=%ld PullReply=%ld\n"
		" DebugLossPacket: Notify=%ld Connect=%ld Truc=%ld NackLib=%ld NackMCP=%ld Raw=%ld\n",
		(unsigned long) omx_debug,
		omx_TINY_packet_loss, omx_SMALL_packet_loss, omx_MEDIUM_FRAG_packet_loss, omx_RNDV_packet_loss,	omx_PULL_REQ_packet_loss, omx_PULL_REPLY_packet_loss,
		omx_NOTIFY_packet_loss, omx_CONNECT_packet_loss, omx_TRUC_packet_loss, omx_NACK_LIB_packet_loss, omx_NACK_MCP_packet_loss, omx_RAW_packet_loss);
#else
	len = snprintf(tmp, OMX_DRIVER_STRING_LEN-buflen,
		" Debug: Disabled\n");
#endif
	tmp += len;
	buflen += len;

	*lenp = buflen+1; /* add the ending \0 */
	return buffer;
}

static __init int
omx_init(void)
{
	int ret;

	printk(KERN_INFO "Open-MX " VERSION " initializing...\n");

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
	omx_driver_userdesc->packet_ring_entry_shift = OMX_PACKET_RING_ENTRY_SHIFT;
	omx_driver_userdesc->hz = HZ;
	omx_driver_userdesc->jiffies = jiffies;
	omx_driver_userdesc->peer_table_configured = 0;
	omx_driver_userdesc->peer_table_version = 0;
	omx_driver_userdesc->peer_table_size = 0;
	omx_driver_userdesc->peer_table_mapper_id = -1;

	/* setup driver feature mask */
	omx_driver_userdesc->features = 0;
#ifdef OMX_MX_WIRE_COMPAT
	omx_driver_userdesc->features |= OMX_DRIVER_FEATURE_WIRECOMPAT;
#endif
#ifndef OMX_DISABLE_SHARED
	omx_driver_userdesc->features |= OMX_DRIVER_FEATURE_SHARED;
#endif
#ifdef CONFIG_MMU_NOTIFIER
	if (omx_pin_invalidate)
		omx_driver_userdesc->features |= OMX_DRIVER_FEATURE_PIN_INVALIDATE;
#endif
#ifdef OMX_MX_WIRE_COMPAT
	omx_driver_userdesc->mtu = 4096+64; /* roughly MX "mtu" */
#else
	omx_driver_userdesc->mtu = OMX_MTU;
#endif

	if (omx_endpoint_max > OMX_ENDPOINT_INDEX_MAX) {
		printk(KERN_INFO "Open-MX: Cannot use more than %d endpoints per board\n",
		       OMX_ENDPOINT_INDEX_MAX);
		ret = -EINVAL;
		goto out_with_driver_userdesc;
	}
	if (omx_peer_max > OMX_PEER_INDEX_MAX) {
		printk(KERN_INFO "Open-MX: Cannot use more than %d peers\n",
		       OMX_PEER_INDEX_MAX);
		ret = -EINVAL;
		goto out_with_driver_userdesc;
	}
	if (omx_skb_frags > MAX_SKB_FRAGS) {
		printk(KERN_INFO "Open-MX: Cannot use more than MAX_SKB_FRAGS (%ld) skb frags\n",
		       (unsigned long) MAX_SKB_FRAGS);
		ret = -EINVAL;
		goto out_with_driver_userdesc;
	}

	/* setup a timer to update jiffies in the driver user descriptor */
	setup_timer(&omx_driver_userdesc_update_timer, omx_driver_userdesc_update_handler, 0);
	__mod_timer(&omx_driver_userdesc_update_timer, jiffies+1);

	ret = omx_dma_init();
	if (ret < 0)
		goto out_with_timer;

	ret = omx_peers_init();
	if (ret < 0)
		goto out_with_dma;

	ret = omx_net_init();
	if (ret < 0)
		goto out_with_peers;

	omx_kthread_task = kthread_run(omx_kthread_func, NULL, "open-mxd");
	if (IS_ERR(omx_kthread_func)) {
		ret = PTR_ERR(omx_kthread_task);
		goto out_with_net;
	}

	ret = omx_raw_init();
	if (ret < 0)
		goto out_with_kthread;

	ret = omx_dev_init();
	if (ret < 0)
		goto out_with_raw;

	printk(KERN_INFO "Open-MX initialized\n");
	return 0;

 out_with_raw:
	omx_raw_exit();
 out_with_kthread:
	kthread_stop(omx_kthread_task);
 out_with_net:
	omx_net_exit();
 out_with_peers:
	omx_peers_init();
 out_with_dma:
	omx_dma_exit();
 out_with_timer:
	del_timer_sync(&omx_driver_userdesc_update_timer);
 out_with_driver_userdesc:
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
	omx_raw_exit();
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
