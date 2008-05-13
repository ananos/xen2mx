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
#include <linux/poll.h>
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

#ifdef OMX_DRIVER_DEBUG
/* defined as module parameters */
extern unsigned long omx_RAW_packet_loss;
/* index between 0 and the above limit */
static unsigned long omx_RAW_packet_loss_index = 0;
#endif /* OMX_DRIVER_DEBUG */

struct omx_raw_event {
	int status;
	uint64_t context;
	struct list_head list_elt;
	int data_length;
	char data[0];
};

/**********************************
 * Init/Finish the Raw of an Iface
 */

void
omx_iface_raw_init(struct omx_iface_raw * raw)
{
	raw->in_use = 0;
	spin_lock_init(&raw->event_lock);
	INIT_LIST_HEAD(&raw->event_list);
	raw->event_list_length = 0;
	init_waitqueue_head(&raw->event_wq);
}

void
omx_iface_raw_exit(struct omx_iface_raw * raw)
{
	struct omx_raw_event *event, *next;
	spin_lock_bh(&raw->event_lock);
	list_for_each_entry_safe(event, next, &raw->event_list, list_elt) {
		list_del(&event->list_elt);
		kfree(event);
	}
	raw->event_list_length = 0;
	spin_unlock_bh(&raw->event_lock);
}

/*******************
 * Send Raw Packets
 */

static int
omx_raw_send(struct omx_iface *iface, void __user * uparam)
{
	struct omx_iface_raw *raw = &iface->raw;
	struct omx_cmd_raw_send raw_send;
	struct omx_raw_event *event = NULL;
	struct sk_buff *skb;
	int ret;

	ret = copy_from_user(&raw_send, uparam, sizeof(raw_send));
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	ret = -ENOMEM;
	skb = omx_new_skb(raw_send.buffer_length);
	if (!skb)
		goto out;

	ret = copy_from_user(omx_skb_mac_header(skb), (void __user *)(unsigned long) raw_send.buffer, raw_send.buffer_length);
	if (ret) {
		ret = -EFAULT;
		goto out_with_skb;
	}

	if (raw_send.need_event) {
		ret = -ENOMEM;
		event = kmalloc(sizeof(*event), GFP_KERNEL);
		if (!event)
			goto out_with_skb;

		event->status = OMX_CMD_RAW_EVENT_SEND_COMPLETE;
		event->data_length = 0;
		event->context = raw_send.context;

		spin_lock_bh(&raw->event_lock);
		list_add_tail(&event->list_elt, &raw->event_list);
		raw->event_list_length++;
		wake_up_interruptible(&raw->event_wq);
		spin_unlock_bh(&raw->event_lock);
	}

	omx_queue_xmit(iface, skb, RAW);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
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

	if (raw->event_list_length > OMX_RAW_RECVQ_LEN) {
		dev_kfree_skb(skb);
		omx_counter_inc(iface, DROP_RAW_QUEUE_FULL);
	} else if (skb->len > OMX_RAW_PKT_LEN_MAX) {
		dev_kfree_skb(skb);
		omx_counter_inc(iface, DROP_RAW_TOO_LARGE);
	} else {
		struct omx_raw_event *event;
		int length = min((unsigned) skb->len, (unsigned) OMX_RAW_PKT_LEN_MAX);

		event = kmalloc(sizeof(*event) + length, GFP_ATOMIC);
		if (!event)
			return -ENOMEM;

		event->status = OMX_CMD_RAW_EVENT_RECV_COMPLETE;
		event->data_length = length;
		skb_copy_bits(skb, 0, event->data, length);
		dev_kfree_skb(skb);

		spin_lock(&raw->event_lock);
		list_add_tail(&event->list_elt, &raw->event_list);
		raw->event_list_length++;
		wake_up_interruptible(&raw->event_wq);
		spin_unlock(&raw->event_lock);

	        omx_counter_inc(iface, RECV_RAW);
	}

	return 0;
}

static int
omx_raw_get_event(struct omx_iface_raw * raw, void __user * uparam)
{
	struct omx_cmd_raw_get_event get_event;
	DEFINE_WAIT(__wait);
	unsigned long timeout;
	int err;

	err = copy_from_user(&get_event, uparam, sizeof(get_event));
	if (err)
		return -EFAULT;

	timeout = msecs_to_jiffies(get_event.timeout);
	get_event.status = OMX_CMD_RAW_NO_EVENT;

	spin_lock_bh(&raw->event_lock);
	while (timeout > 0) {
		prepare_to_wait(&raw->event_wq, &__wait, TASK_INTERRUPTIBLE);

		if (raw->event_list_length)
			/* got an event */
			break;

		if (signal_pending(current))
			/* got interrupted */
			break;

		spin_unlock_bh(&raw->event_lock);
		timeout = schedule_timeout(timeout);
		spin_lock_bh(&raw->event_lock);
	}
	finish_wait(&raw->event_wq, &__wait);

	if (!raw->event_list_length) {
		spin_unlock_bh(&raw->event_lock);
		/* got a timeout or interrupted */

	} else {
		struct omx_raw_event * event;

		event = list_entry(raw->event_list.next, struct omx_raw_event, list_elt);
		list_del(&event->list_elt);
		raw->event_list_length--;
		spin_unlock_bh(&raw->event_lock);

		/* fill the event */
		get_event.status = event->status;
		get_event.context = event->context;
		get_event.buffer_length = event->data_length;

		/* copy into user-space */
		err = copy_to_user((void __user *)(unsigned long) get_event.buffer,
				   event->data, event->data_length);
		if (err) {
			err = -EFAULT;
			kfree(event);
			goto out;
		}

		kfree(event);
	}

	get_event.timeout = jiffies_to_msecs(timeout);

	err = copy_to_user(uparam, &get_event, sizeof(get_event));
	if (err) {
		err = -EFAULT;
		goto out;
	}

	return 0;

 out:
	return err;
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

	case OMX_CMD_RAW_GET_EVENT: {
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

static unsigned int
omx_raw_miscdev_poll(struct file *file, struct poll_table_struct *wait)
{
	struct omx_iface *iface;
	struct omx_iface_raw *raw;
	unsigned int mask = 0;

	iface = file->private_data;
	if (!iface) {
		mask |= POLLERR;
		goto out;
	}
	raw = &iface->raw;

	poll_wait(file, &raw->event_wq, wait);
	if (raw->event_list_length)
		mask |= POLLIN;

 out:
	return mask;
}

static struct file_operations
omx_raw_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = omx_raw_miscdev_open,
	.release = omx_raw_miscdev_release,
	.unlocked_ioctl = omx_raw_miscdev_ioctl,
	.poll = omx_raw_miscdev_poll,
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
