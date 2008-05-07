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
#include <linux/mm.h>
#include <asm/uaccess.h>

#include "omx_hal.h"
#include "omx_io.h"
#include "omx_common.h"
#include "omx_misc.h"
#include "omx_iface.h"
#include "omx_endpoint.h"
#include "omx_peer.h"

/**********************************
 * Init/Finish the Raw of an Iface
 */

void
omx_iface_raw_init(struct omx_iface_raw * raw)
{
	raw->in_use = 0;
	skb_queue_head_init(&raw->recv_list);
	init_waitqueue_head(&raw->recv_wq);
}

void
omx_iface_raw_exit(struct omx_iface_raw * raw)
{
	skb_queue_purge(&raw->recv_list);
}

/*******************
 * Send Raw Packets
 */

static int
omx_raw_send(struct omx_iface *iface, void __user * uparam)
{
	struct omx_cmd_raw_send raw_send;
	struct sk_buff *skb;
	int ret;

	ret = copy_from_user(&raw_send, uparam, sizeof(raw_send));
	if (ret)
		return -EFAULT;

	skb = omx_new_skb(raw_send.buffer_length);
	if (!skb)
		return -ENOMEM;

	ret = copy_from_user(omx_skb_mac_header(skb), (void __user *) raw_send.buffer, raw_send.buffer_length);
	if (ret) {
		kfree_skb(skb);
		return -EFAULT;
	}

	omx_queue_xmit(iface, skb, RAW);
	return 0;
}

/**********************
 * Receive Raw Packets
 */

int
omx_recv_raw(struct omx_iface * iface,
	     struct omx_hdr * mh,
	     struct sk_buff * skb)
{
	struct omx_iface_raw *raw = &iface->raw;

	if (skb_queue_len(&raw->recv_list) > OMX_RAW_RECVQ_LEN) {
		dev_kfree_skb(skb);
		omx_counter_inc(iface, DROP_RAW_QUEUE_FULL);
	} else if (skb->len > OMX_RAW_PKT_LEN_MAX) {
		dev_kfree_skb(skb);
		omx_counter_inc(iface, DROP_RAW_TOO_LARGE);
	} else {
		skb_queue_tail(&raw->recv_list, skb);
	        omx_counter_inc(iface, RECV_RAW);
	}

	return 0;
}

static int
omx_raw_get_event(struct omx_iface_raw * raw, void __user * uparam)
{
	struct omx_cmd_raw_recv raw_recv;
	struct sk_buff *skb;
	unsigned long timeout;
	int err;

	err = copy_from_user(&raw_recv, uparam, sizeof(raw_recv));
	if (err)
		return -EFAULT;

	timeout = raw_recv.timeout;

 retry:
	timeout = wait_event_interruptible_timeout(raw->recv_wq,
						   !skb_queue_empty(&raw->recv_list),
						   timeout);
	skb = skb_dequeue(&raw->recv_list);
	if (!skb && timeout && !signal_pending(current))
		goto retry;

	if (skb) {
		/* we got a skb */
		char buffer[OMX_RAW_PKT_LEN_MAX];
		int length = min(skb->len, raw_recv.buffer_length);

		/* copy in a linear buffer */
		skb_copy_bits(skb, 0, buffer, length);
		dev_kfree_skb(skb);

		/* copy into user-space */
		err = copy_to_user((void __user *) raw_recv.buffer, buffer, length);
		if (err) {
			err = -EFAULT;
		} else {
			raw_recv.status = 1;
			raw_recv.buffer_length = length;
		}

	} else {
		/* got a timeout or got interrupted */
		raw_recv.status  = 0;
	}

	raw_recv.timeout = timeout;

	err = copy_to_user(uparam, &raw_recv, sizeof(raw_recv));
	if (err)
		return -EFAULT;

	return 0;
}

/****************************
 * Raw MiscDevice operations
 */

static int
omx_raw_miscdev_open(struct inode * inode, struct file * file)
{
	file->private_data = NULL;
	return 0;
}

static int
omx_raw_miscdev_release(struct inode * inode, struct file * file)
{
	struct omx_iface *iface;

	iface = rcu_dereference(file->private_data);
	if (!iface)
		return -EINVAL;

	return omx_raw_detach_iface(iface);
}

static long
omx_raw_miscdev_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct omx_iface *iface;
	int err = 0;

	switch (cmd) {
	case OMX_CMD_RAW_OPEN_ENDPOINT: {
		struct omx_cmd_raw_open_endpoint raw_open;

		err = copy_from_user(&raw_open, (void __user *) arg, sizeof(raw_open));
		if (err) {
			err = -EFAULT;
			goto out;
		}

		err = omx_raw_attach_iface(raw_open.board_index, (struct omx_iface **) &file->private_data);
		break;
	}

	case OMX_CMD_RAW_SEND: {
		err = -EBADF;
		iface = file->private_data;
		if (!iface)
			goto out;

		err = omx_raw_send(iface, (void __user *) arg);
		break;
	}

	case OMX_CMD_RAW_RECV: {
		err = -EBADF;
		iface = file->private_data;
		if (!iface)
			goto out;

		err = omx_raw_get_event(&iface->raw, (void __user *) arg);
		break;
	}

	default:
		err = -ENOSYS;
		break;
	}

 out:
	return err;
}

static struct file_operations
omx_raw_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = omx_raw_miscdev_open,
	.release = omx_raw_miscdev_release,
	.unlocked_ioctl = omx_raw_miscdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = omx_raw_miscdev_ioctl,
#endif
};

static struct miscdevice
omx_raw_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "open-mx-raw",
	.fops = &omx_raw_miscdev_fops,
};

/******************************
 * Device registration
 */

int
omx_raw_init(void)
{
	int ret;

	ret = misc_register(&omx_raw_miscdev);
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: Failed to register raw misc device, error %d\n", ret);
		goto out;
	}

	return 0;

 out:
	return ret;
}

void
omx_raw_exit(void)
{
	misc_deregister(&omx_raw_miscdev);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
