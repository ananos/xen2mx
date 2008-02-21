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
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <asm/uaccess.h>

#include "omx_hal.h"
#include "omx_io.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_peer.h"
#include "omx_endpoint.h"
#include "omx_region.h"

/******************************
 * Alloc/Release internal endpoint fields once everything is setup/locked
 */

static int
omx_endpoint_alloc_resources(struct omx_endpoint * endpoint)
{
	struct page ** sendq_pages, ** recvq_pages;
	struct omx_endpoint_desc *userdesc;
	char * buffer;
	int i;
	int ret;

	/* generate the session id */
	get_random_bytes(&endpoint->session_id, sizeof(endpoint->session_id));

	/* create the user descriptor */
	userdesc = omx_vmalloc_user(sizeof(struct omx_endpoint_desc));
	if (!userdesc) {
		printk(KERN_ERR "Open-MX: failed to allocate endpoint user descriptor\n");
		ret = -ENOMEM;
		goto out;
	}
	userdesc->status = 0;
	userdesc->session_id = endpoint->session_id;
	endpoint->userdesc = userdesc;

	/* alloc and init user queues */
	ret = -ENOMEM;
	buffer = omx_vmalloc_user(OMX_SENDQ_SIZE + OMX_RECVQ_SIZE + OMX_EXP_EVENTQ_SIZE + OMX_UNEXP_EVENTQ_SIZE);
	if (!buffer) {
		printk(KERN_ERR "Open-MX: failed to allocate queues\n");
		goto out_with_desc;
	}
	endpoint->sendq = buffer;
	endpoint->recvq = endpoint->sendq + OMX_SENDQ_SIZE;
	endpoint->exp_eventq = endpoint->recvq + OMX_RECVQ_SIZE;
	endpoint->unexp_eventq = endpoint->exp_eventq + OMX_EXP_EVENTQ_SIZE;

#if OMX_SENDQ_ENTRY_SIZE != PAGE_SIZE
#error Incompatible page and sendq entry sizes
#endif
	sendq_pages = kmalloc(OMX_SENDQ_ENTRY_NR * sizeof(struct page *), GFP_KERNEL);
	if (!sendq_pages) {
		printk(KERN_ERR "Open-MX: failed to allocate sendq pages array\n");
		goto out_with_userq;
	}
	for(i=0; i<OMX_SENDQ_ENTRY_NR; i++) {
		struct page * page;
		page = vmalloc_to_page(endpoint->sendq + (i << OMX_SENDQ_ENTRY_SHIFT));
		BUG_ON(!page);
		sendq_pages[i] = page;
	}
	endpoint->sendq_pages = sendq_pages;

	recvq_pages = kmalloc(OMX_RECVQ_ENTRY_NR * sizeof(struct page *), GFP_KERNEL);
	if (!recvq_pages) {
		printk(KERN_ERR "Open-MX: failed to allocate recvq pages array\n");
		goto out_with_sendq_pages;
	}
	for(i=0; i<OMX_RECVQ_ENTRY_NR; i++) {
		struct page * page;
		page = vmalloc_to_page(endpoint->recvq + (i << OMX_RECVQ_ENTRY_SHIFT));
		BUG_ON(!page);
		recvq_pages[i] = page;
	}
	endpoint->recvq_pages = recvq_pages;

	/* finish initializing queues */
	omx_endpoint_queues_init(endpoint);

	/* initialize user regions */
	omx_endpoint_user_regions_init(endpoint);

	/* initialize pull handles */
	omx_endpoint_pull_handles_init(endpoint);

	return 0;

 out_with_sendq_pages:
	kfree(endpoint->sendq_pages);
 out_with_userq:
	vfree(endpoint->sendq); /* recvq and eventq are in the same buffer */
 out_with_desc:
	vfree(endpoint->userdesc);
 out:
	return ret;
}

static void
omx_endpoint_free_resources(struct omx_endpoint * endpoint)
{
	might_sleep();

	omx_endpoint_user_regions_exit(endpoint);
	omx_endpoint_pull_handles_force_exit(endpoint);
	kfree(endpoint->recvq_pages);
	kfree(endpoint->sendq_pages);
	vfree(endpoint->sendq); /* recvq, exp_eventq and unexp_eventq are in the same buffer */
	vfree(endpoint->userdesc);
}

/****************************
 * Endpoint Deferred Release
 */

/* vfree cannot be called from BH, so we just
 * let the cleanup thread take care of it by moving
 * endpoints to a cleanup list first
 */
static spinlock_t omx_endpoints_cleanup_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(omx_endpoints_cleanup_list);

/* Called when the last reference on the endpoint is released */
void
__omx_endpoint_last_release(struct kref *kref)
{
	struct omx_endpoint * endpoint = container_of(kref, struct omx_endpoint, refcount);
	struct omx_iface * iface = endpoint->iface;

	dprintk(KREF, "releasing the last reference on endpoint %d for iface %s (%s)\n",
		endpoint->endpoint_index, iface->peer.hostname, iface->eth_ifp->name);

	endpoint->iface = NULL;
	omx_iface_release(iface);

	spin_lock(&omx_endpoints_cleanup_lock);
	list_add(&endpoint->list_elt, &omx_endpoints_cleanup_list);
	spin_unlock(&omx_endpoints_cleanup_lock);
}

void
omx_endpoints_cleanup(void)
{
        struct omx_endpoint * endpoint, * next;
	LIST_HEAD(private_head);

	/* move the whole list to our private head at once */
	spin_lock_bh(&omx_endpoints_cleanup_lock);
	list_splice(&omx_endpoints_cleanup_list, &private_head);
	INIT_LIST_HEAD(&omx_endpoints_cleanup_list);
	spin_unlock_bh(&omx_endpoints_cleanup_lock);

	/* and now free all endpoints without needing any lock */
	list_for_each_entry_safe(endpoint, next, &private_head, list_elt) {
		/*
		 * only that were open before end up here,
		 * the global fd is freed in omx_endpoint_close()
		 */
		BUG_ON(endpoint->status != OMX_ENDPOINT_STATUS_CLOSING);
		omx_endpoint_free_resources(endpoint);
		kfree(endpoint);
	}
}

/******************************
 * Opening/Closing endpoint main routines
 */

static int
omx_endpoint_open(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct omx_cmd_open_endpoint param;
	struct net_device *ifp;
	int ret;

	ret = copy_from_user(&param, uparam, sizeof(param));
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: Failed to read open endpoint command argument, error %d\n", ret);
		goto out;
	}
	endpoint->board_index = param.board_index;
	endpoint->endpoint_index = param.endpoint_index;

	/* test whether the endpoint is ok to be open
	 * and mark it as initializing */
	spin_lock(&endpoint->status_lock);
	ret = -EINVAL;
	if (endpoint->status != OMX_ENDPOINT_STATUS_FREE) {
		spin_unlock(&endpoint->status_lock);
		goto out;
	}
	endpoint->status = OMX_ENDPOINT_STATUS_INITIALIZING;
	spin_unlock(&endpoint->status_lock);

	/* alloc internal fields */
	ret = omx_endpoint_alloc_resources(endpoint);
	if (ret < 0)
		goto out_with_init;

	/* attach the endpoint to the iface */
	ret = omx_iface_attach_endpoint(endpoint);
	if (ret < 0)
		goto out_with_resources;

	endpoint->opener_pid = current->pid;
	strncpy(endpoint->opener_comm, current->comm, TASK_COMM_LEN);

	/* check iface status */
	ifp = endpoint->iface->eth_ifp;
	if (!(dev_get_flags(ifp) & IFF_UP))
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_IFACE_DOWN;
	if (ifp->mtu < OMX_MTU_MIN)
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_IFACE_BAD_MTU;

	return 0;

 out_with_resources:
	omx_endpoint_free_resources(endpoint);
 out_with_init:
	endpoint->status = OMX_ENDPOINT_STATUS_FREE;
 out:
	return ret;
}

/* Detach the endpoint and release the reference on it.
 * If already closing, return an error.
 */
int
omx_endpoint_close(struct omx_endpoint * endpoint,
		   int ifacelocked)
{
	int ret;

	spin_lock(&endpoint->status_lock);

	/* test whether the endpoint is ok to be closed */

	if (endpoint->status == OMX_ENDPOINT_STATUS_FREE) {
		/* not open, just free the structure */
		spin_unlock(&endpoint->status_lock);
		kfree(endpoint);
		return 0;
	}

	ret = -EINVAL;
	if (endpoint->status != OMX_ENDPOINT_STATUS_OK) {
		/* either already closing or not initialized yet */
		spin_unlock(&endpoint->status_lock);
		goto out;
	}

	/* mark it as closing so that nobody may use it again */
	endpoint->status = OMX_ENDPOINT_STATUS_CLOSING;

	spin_unlock(&endpoint->status_lock);

	/* detach from the iface now so that nobody can acquire it */
	omx_iface_detach_endpoint(endpoint, ifacelocked);
	/* but keep the endpoint->iface valid until everybody releases the endpoint */

	/* schedule the releasing of pending pull handles */
	omx_endpoint_pull_handles_prepare_exit(endpoint);

	/* release our refcount now that other users cannot use again */
	kref_put(&endpoint->refcount, __omx_endpoint_last_release);

	return 0;

 out:
	return ret;
}

/******************************
 * Acquiring/Releasing endpoints
 */

/* maybe called by the bottom half */
struct omx_endpoint *
omx_endpoint_acquire_by_iface_index(struct omx_iface * iface, uint8_t index)
{
	struct omx_endpoint * endpoint;
	int err;

	rcu_read_lock();

	if (unlikely(index >= omx_endpoint_max)) {
		err = -EINVAL;
		goto out_with_rcu_lock;
	}

	endpoint = rcu_dereference(iface->endpoints[index]);
	if (unlikely(!endpoint)) {
		err = -ENOENT;
		goto out_with_rcu_lock;
	}

	/*
	 * no need to lock the endpoint status, just do things in the right order:
	 * take a reference first, check the status and release if we were wrong
	 */
	kref_get(&endpoint->refcount);

	if (unlikely(endpoint->status != OMX_ENDPOINT_STATUS_OK)) {
		err = -ENOENT;
		goto out_with_kref;
	}

	rcu_read_unlock();
	return endpoint;

 out_with_kref:
	kref_put(&endpoint->refcount, __omx_endpoint_last_release);
 out_with_rcu_lock:
	rcu_read_unlock();
	return ERR_PTR(err);
}

/******************************
 * File operations
 */

static int
omx_miscdev_open(struct inode * inode, struct file * file)
{
	struct omx_endpoint * endpoint;

	endpoint = kmalloc(sizeof(struct omx_endpoint), GFP_KERNEL);
	if (!endpoint)
		return -ENOMEM;

	kref_init(&endpoint->refcount);
	spin_lock_init(&endpoint->status_lock);
	endpoint->status = OMX_ENDPOINT_STATUS_FREE;

	file->private_data = endpoint;
	return 0;
}

static int
omx_miscdev_release(struct inode * inode, struct file * file)
{
	struct omx_endpoint * endpoint = file->private_data;

	BUG_ON(!endpoint);

	/*
	 * if really closing an endpoint, omx_endpoint_close() may fail if already being closed.
	 * if closing the global fd, it will fail for sure, but we don't care.
	 * just try to close, let omx_endpoint_close() fail if needed, and ignore the return value.
	 */
	omx_endpoint_close(endpoint, 0); /* we don't hold the iface lock */

	return 0;
}

/*
 * Common command handlers.
 * Use OMX_CMD_INDEX() to only keep the 8 latest bits of the 32bits command flags.
 * Handlers are numbered starting from 0 (for OMX_CMD_BENCH).
 * OMX_CMD_BENCH must be the first endpoint-based ioctl and the other ones
 * must use contigous numbers.
 */

#define OMX_CMD_HANDLER_SHIFT(index) (index - OMX_CMD_INDEX(OMX_CMD_BENCH))
#define OMX_CMD_HANDLER_OFFSET(cmd) OMX_CMD_HANDLER_SHIFT(OMX_CMD_INDEX(cmd))

static int (*omx_ioctl_with_endpoint_handlers[])(struct omx_endpoint * endpoint, void __user * uparam) = {
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_BENCH)]			= omx_ioctl_bench,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_TINY)]		= omx_ioctl_send_tiny,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_SMALL)]		= omx_ioctl_send_small,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_MEDIUM)]		= omx_ioctl_send_medium,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_RNDV)]		= omx_ioctl_send_rndv,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_PULL)]			= omx_ioctl_pull,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_NOTIFY)]		= omx_ioctl_send_notify,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_CONNECT)]	       	= omx_ioctl_send_connect,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_SEND_TRUC)]		= omx_ioctl_send_truc,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_REGISTER_REGION)]	= omx_ioctl_user_region_register,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_DEREGISTER_REGION)]	= omx_ioctl_user_region_deregister,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_WAIT_EVENT)]		= omx_ioctl_wait_event,
	[OMX_CMD_HANDLER_OFFSET(OMX_CMD_WAKEUP)]		= omx_ioctl_wakeup,
};

/* call the ioctl handler assuming the caller checked the index */
static INLINE long
omx_handle_ioctl_with_endpoint(struct file *file, unsigned handler_offset, void __user * uparam)
{
	struct omx_endpoint * endpoint = file->private_data;

	/*
	 * the endpoint is already acquired by the file,
	 * just check its status
	 */
	if (unlikely(endpoint->status != OMX_ENDPOINT_STATUS_OK))
		return -EINVAL;

	return omx_ioctl_with_endpoint_handlers[handler_offset](endpoint, uparam);
}

/*
 * Main ioctl switch where all application ioctls arrive
 */
static long
omx_miscdev_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	unsigned cmd_index = OMX_CMD_INDEX(cmd);
	unsigned handler_offset = OMX_CMD_HANDLER_SHIFT(cmd_index); /* unsigned, so that we don't have to check >= 0 */
	int ret = 0;

#ifndef OMX_DRIVER_DEBUG
	/* optimize the send case */
	if (likely(handler_offset < ARRAY_SIZE(omx_ioctl_with_endpoint_handlers)))
		return omx_handle_ioctl_with_endpoint(file, handler_offset, (void __user *) arg);
#endif

	switch (cmd) {

	case OMX_CMD_GET_BOARD_COUNT: {
		uint32_t count = omx_ifaces_get_count();

		ret = copy_to_user((void __user *) arg, &count,
				   sizeof(count));
		if (ret < 0)
			printk(KERN_ERR "Open-MX: Failed to write get_board_count command result, error %d\n", ret);

		break;
	}

	case OMX_CMD_GET_BOARD_INFO: {
		struct omx_endpoint * endpoint = file->private_data;
		struct omx_cmd_get_board_info get_board_info;

		/*
		 * the endpoint is already acquired by the file,
		 * just check its status
		 */
		ret = -EINVAL;
		if (endpoint->status != OMX_ENDPOINT_STATUS_OK) {
			/* the endpoint is not open, get the command parameter and use its board_index */
			ret = copy_from_user(&get_board_info, (void __user *) arg,
					     sizeof(get_board_info));
			if (ret < 0) {
				printk(KERN_ERR "Open-MX: Failed to read get_board_info command argument, error %d\n", ret);
				goto out;
			}
		} else {
			/* endpoint acquired, use its board index */
			get_board_info.board_index = endpoint->board_index;
		}

		ret = omx_iface_get_info(get_board_info.board_index, &get_board_info.info);
		if (ret < 0)
			goto out;

		ret = copy_to_user((void __user *) arg, &get_board_info,
				   sizeof(get_board_info));
		if (ret < 0)
			printk(KERN_ERR "Open-MX: Failed to write get_board_info command result, error %d\n", ret);

		break;
	}

	case OMX_CMD_GET_ENDPOINT_INFO: {
		struct omx_cmd_get_endpoint_info get_endpoint_info;

		ret = copy_from_user(&get_endpoint_info, (void __user *) arg,
				     sizeof(get_endpoint_info));
		if (ret < 0) {
			printk(KERN_ERR "Open-MX: Failed to read get_endpoint_info command argument, error %d\n", ret);
			goto out;
		}

		ret = omx_endpoint_get_info(get_endpoint_info.board_index, get_endpoint_info.endpoint_index,
					    &get_endpoint_info.info);
		ret = copy_to_user((void __user *) arg, &get_endpoint_info,
				   sizeof(get_endpoint_info));
		if (ret < 0)
			printk(KERN_ERR "Open-MX: Failed to write get_endpoint_info command result, error %d\n", ret);

		break;
	}

	case OMX_CMD_GET_COUNTERS: {
		struct omx_cmd_get_counters get_counters;

		ret = copy_from_user(&get_counters, (void __user *) arg,
				     sizeof(get_counters));
		if (ret < 0) {
			printk(KERN_ERR "Open-MX: Failed to read get_counters command argument, error %d\n", ret);
			goto out;
		}

		ret = -EPERM;
		if (get_counters.clear && !capable(CAP_SYS_ADMIN))
			goto out;

		ret = omx_iface_get_counters(get_counters.board_index,
					     get_counters.clear,
					     get_counters.buffer_addr, get_counters.buffer_length);
		if (ret < 0)
			goto out;

		ret = copy_to_user((void __user *) arg, &get_counters,
				   sizeof(get_counters));
		if (ret < 0)
			printk(KERN_ERR "Open-MX: Failed to write get_counters command result, error %d\n", ret);

		break;
	}

	case OMX_CMD_SET_HOSTNAME: {
		struct omx_cmd_set_hostname set_hostname;

		ret = copy_from_user(&set_hostname, (void __user *) arg,
				     sizeof(set_hostname));
		if (ret < 0) {
			printk(KERN_ERR "Open-MX: Failed to read set_hostname command argument, error %d\n", ret);
			goto out;
		}

		ret = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out;

		set_hostname.hostname[OMX_HOSTNAMELEN_MAX-1] = '\0';

		ret = omx_iface_set_hostname(set_hostname.board_index,
					     set_hostname.hostname);

		break;
	}

	case OMX_CMD_PEERS_CLEAR: {

		ret = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out;

		omx_peers_clear();

		ret = 0;
		break;
	}

	case OMX_CMD_PEER_ADD: {
		struct omx_cmd_misc_peer_info peer_info;

		ret = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out;

		ret = copy_from_user(&peer_info, (void __user *) arg,
				     sizeof(peer_info));
		if (ret < 0) {
			printk(KERN_ERR "Open-MX: Failed to read add_peer command argument, error %d\n", ret);
			goto out;
		}

		peer_info.hostname[OMX_HOSTNAMELEN_MAX-1] = '\0';

		ret = omx_peer_add(peer_info.board_addr, peer_info.hostname);
		break;
	}

	case OMX_CMD_PEER_FROM_INDEX:
	case OMX_CMD_PEER_FROM_ADDR:
	case OMX_CMD_PEER_FROM_HOSTNAME: {
		struct omx_cmd_misc_peer_info peer_info;

		ret = copy_from_user(&peer_info, (void __user *) arg,
				     sizeof(peer_info));
		if (ret < 0) {
			printk(KERN_ERR "Open-MX: Failed to read '%s' command argument, error %d\n",
			       omx_strcmd(cmd), ret);
			goto out;
		}

		if (cmd == OMX_CMD_PEER_FROM_INDEX)
			ret = omx_peer_lookup_by_index(peer_info.index,
						       &peer_info.board_addr, peer_info.hostname);
		else if (cmd == OMX_CMD_PEER_FROM_ADDR)
			ret = omx_peer_lookup_by_addr(peer_info.board_addr,
						      peer_info.hostname, &peer_info.index);
		else if (cmd == OMX_CMD_PEER_FROM_HOSTNAME)
			ret = omx_peer_lookup_by_hostname(peer_info.hostname,
							  &peer_info.board_addr, &peer_info.index);

		if (ret < 0)
			goto out;

		ret = copy_to_user((void __user *) arg, &peer_info,
				   sizeof(peer_info));
		if (ret < 0)
			printk(KERN_ERR "Open-MX: Failed to write '%s' command result, error %d\n",
			       omx_strcmd(cmd), ret);

		break;
	}

	case OMX_CMD_OPEN_ENDPOINT: {
		struct omx_endpoint * endpoint = file->private_data;
		BUG_ON(!endpoint);

		ret = omx_endpoint_open(endpoint, (void __user *) arg);

		break;
	}

	case OMX_CMD_BENCH:
	case OMX_CMD_SEND_TINY:
	case OMX_CMD_SEND_SMALL:
	case OMX_CMD_SEND_MEDIUM:
	case OMX_CMD_SEND_RNDV:
	case OMX_CMD_PULL:
	case OMX_CMD_SEND_NOTIFY:
	case OMX_CMD_SEND_CONNECT:
	case OMX_CMD_SEND_TRUC:
	case OMX_CMD_REGISTER_REGION:
	case OMX_CMD_DEREGISTER_REGION:
	case OMX_CMD_WAIT_EVENT:
	{
		BUG_ON(handler_offset >= ARRAY_SIZE(omx_ioctl_with_endpoint_handlers));
		BUG_ON(omx_ioctl_with_endpoint_handlers[handler_offset] == NULL);

		ret = omx_handle_ioctl_with_endpoint(file, handler_offset, (void __user *) arg);
		break;
	}

	default:
		ret = -ENOSYS;
		break;
	}

 out:
	if (ret != 0)
		dprintk(IOCTL, "cmd %x (%x,%s) returns %d\n",
			cmd, cmd_index, omx_strcmd(cmd), ret);

	return ret;
}

static int
omx_miscdev_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct omx_endpoint * endpoint = file->private_data;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;

	/* endpoint-less ioctl */
	if (offset == OMX_DRIVER_DESC_FILE_OFFSET && size == PAGE_ALIGN(OMX_DRIVER_DESC_SIZE)) {
		if (vma->vm_flags & (VM_WRITE|VM_MAYWRITE))
			return -EPERM;

		return omx_remap_vmalloc_range(vma, omx_driver_userdesc, 0);
	}

	/* the other ioctl require the endpoint to be open */
	if (endpoint->status != OMX_ENDPOINT_STATUS_OK) {
		printk(KERN_INFO "Open-MX: Cannot map endpoint resources from a closed endpoint\n");
		return -EINVAL;
	}

	if (offset == OMX_ENDPOINT_DESC_FILE_OFFSET && size == PAGE_ALIGN(OMX_ENDPOINT_DESC_SIZE))
		return omx_remap_vmalloc_range(vma, endpoint->userdesc, 0);

	else if (offset == OMX_SENDQ_FILE_OFFSET && size == OMX_SENDQ_SIZE)
		return omx_remap_vmalloc_range(vma, endpoint->sendq,
					       0);
	else if (offset == OMX_RECVQ_FILE_OFFSET && size == OMX_RECVQ_SIZE)
		return omx_remap_vmalloc_range(vma, endpoint->sendq,
					       OMX_SENDQ_SIZE >> PAGE_SHIFT);
	else if (offset == OMX_EXP_EVENTQ_FILE_OFFSET && size == OMX_EXP_EVENTQ_SIZE)
		return omx_remap_vmalloc_range(vma, endpoint->sendq,
					       (OMX_SENDQ_SIZE + OMX_RECVQ_SIZE) >> PAGE_SHIFT);
	else if (offset == OMX_UNEXP_EVENTQ_FILE_OFFSET && size == OMX_UNEXP_EVENTQ_SIZE)
		return omx_remap_vmalloc_range(vma, endpoint->sendq,
					       (OMX_SENDQ_SIZE + OMX_RECVQ_SIZE + OMX_EXP_EVENTQ_SIZE) >> PAGE_SHIFT);
	else {
		printk(KERN_ERR "Open-MX: Cannot mmap 0x%lx at 0x%lx\n", size, offset);
		return -EINVAL;
	}
}

static struct file_operations
omx_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = omx_miscdev_open,
	.release = omx_miscdev_release,
	.mmap = omx_miscdev_mmap,
	.unlocked_ioctl = omx_miscdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = omx_miscdev_ioctl,
#endif
};

static struct miscdevice
omx_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "open-mx",
	.fops = &omx_miscdev_fops,
};

/******************************
 * Device attributes
 */

#ifdef OMX_MISCDEV_HAVE_CLASS_DEVICE

static ssize_t
omx_ifaces_attr_show(struct class_device *dev, char *buf)
{
	return omx_ifaces_show(buf);
}

static ssize_t
omx_ifaces_attr_store(struct class_device *dev, const char *buf, size_t size)
{
	return omx_ifaces_store(buf, size);
}

static CLASS_DEVICE_ATTR(ifaces, S_IRUGO|S_IWUSR, omx_ifaces_attr_show, omx_ifaces_attr_store);

static int
omx_init_attributes(void)
{
	return class_device_create_file(omx_miscdev.class, &class_device_attr_ifaces);
}

static void
omx_exit_attributes(void)
{
	class_device_remove_file(omx_miscdev.class, &class_device_attr_ifaces);
}

#else /* !OMX_MISCDEV_HAVE_CLASS_DEVICE */

static ssize_t
omx_ifaces_attr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return omx_ifaces_show(buf);
}

static ssize_t
omx_ifaces_attr_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return omx_ifaces_store(buf, size);
}

static DEVICE_ATTR(ifaces, S_IRUGO|S_IWUSR, omx_ifaces_attr_show, omx_ifaces_attr_store);

static int
omx_init_attributes(void)
{
	return device_create_file(omx_miscdev.this_device, &dev_attr_ifaces);
}

static void
omx_exit_attributes(void)
{
	device_remove_file(omx_miscdev.this_device, &dev_attr_ifaces);
}

#endif /* !OMX_MISCDEV_HAVE_CLASS_DEVICE */


/******************************
 * Device registration
 */

int
omx_dev_init(void)
{
	int ret;

#ifdef OMX_DRIVER_DEBUG
	/* check that there's no hole in the endpoint-based ioctl values */
	int i;
	for(i=0; i<ARRAY_SIZE(omx_ioctl_with_endpoint_handlers); i++)
		if (omx_ioctl_with_endpoint_handlers[i] == NULL) {
			printk(KERN_ERR "Open-MX: Found a hole in the array of endpoint-based ioctl handlers at offset %d\n", i);
			return -EINVAL;
		}
#endif

	ret = misc_register(&omx_miscdev);
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: Failed to register misc device, error %d\n", ret);
		goto out;
	}

	ret = omx_init_attributes();
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: failed to create misc device attributes, error %d\n", ret);
		goto out_with_device;
	}

	return 0;

 out_with_device:
	misc_deregister(&omx_miscdev);
 out:
	return ret;
}

void
omx_dev_exit(void)
{
	omx_exit_attributes();
	misc_deregister(&omx_miscdev);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
