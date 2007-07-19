#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

#include "mpoe_hal.h"
#include "mpoe_io.h"
#include "mpoe_common.h"

/******************************
 * Alloc/Release internal endpoint fields once everything is setup/locked
 */

static int
mpoe_endpoint_alloc_resources(struct mpoe_endpoint * endpoint)
{
	union mpoe_evt * evt;
	char * buffer;
	int ret;

	/* alloc and init user queues */
	ret = -ENOMEM;
	buffer = mpoe_vmalloc_user(MPOE_SENDQ_SIZE + MPOE_RECVQ_SIZE + MPOE_EVENTQ_SIZE);
	if (!buffer) {
		printk(KERN_ERR "MPoE: failed to allocate queues\n");
		goto out;
	}
	endpoint->sendq = buffer;
	endpoint->recvq = buffer + MPOE_SENDQ_SIZE;
	endpoint->eventq = buffer + MPOE_SENDQ_SIZE + MPOE_RECVQ_SIZE;

	for(evt = endpoint->eventq;
	    (void *) evt < endpoint->eventq + MPOE_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = MPOE_EVT_NONE;
	endpoint->next_eventq_slot = endpoint->eventq;
	endpoint->next_recvq_slot = endpoint->recvq;

	/* initialize user regions */
	mpoe_endpoint_user_regions_init(endpoint);

	/* initialize pull handles */
	mpoe_endpoint_pull_handles_init(endpoint);

	return 0;

 out:
	return ret;
}

static void
mpoe_endpoint_free_resources(struct mpoe_endpoint * endpoint)
{
	mpoe_endpoint_pull_handles_exit(endpoint);
	mpoe_endpoint_user_regions_exit(endpoint);
	vfree(endpoint->sendq); /* recvq and eventq are in the same buffer */
}

/******************************
 * Opening/Closing endpoint main routines
 */

static int
mpoe_endpoint_open(struct mpoe_endpoint * endpoint, void __user * uparam)
{
	struct mpoe_cmd_open_endpoint param;
	int ret;

	ret = copy_from_user(&param, uparam, sizeof(param));
	if (ret < 0) {
		printk(KERN_ERR "MPoE: Failed to read open endpoint command argument, error %d\n", ret);
		goto out;
	}
	endpoint->board_index = param.board_index;
	endpoint->endpoint_index = param.endpoint_index;

	/* test whether the endpoint is ok to be open
	 * and mark it as initializing */
	spin_lock(&endpoint->lock);
	ret = -EINVAL;
	if (endpoint->status != MPOE_ENDPOINT_STATUS_FREE) {
		spin_unlock(&endpoint->lock);
		goto out;
	}
	endpoint->status = MPOE_ENDPOINT_STATUS_INITIALIZING;
	atomic_inc(&endpoint->refcount);
	spin_unlock(&endpoint->lock);

	/* alloc internal fields */
	ret = mpoe_endpoint_alloc_resources(endpoint);
	if (ret < 0)
		goto out_with_init;

	/* attach the endpoint to the iface */
	ret = mpoe_iface_attach_endpoint(endpoint);
	if (ret < 0)
		goto out_with_resources;

	printk(KERN_INFO "MPoE: Successfully open board %d endpoint %d\n",
	       endpoint->board_index, endpoint->endpoint_index);

	return 0;

 out_with_resources:
	mpoe_endpoint_free_resources(endpoint);
 out_with_init:
	atomic_dec(&endpoint->refcount);
	endpoint->status = MPOE_ENDPOINT_STATUS_FREE;
 out:
	return ret;
}

/* Wait for all users to release an endpoint and then close it.
 * If already closing, return -EBUSY.
 */
int
__mpoe_endpoint_close(struct mpoe_endpoint * endpoint,
		      int ifacelocked)
{
	DECLARE_WAITQUEUE(wq, current);
	int ret;

	/* test whether the endpoint is ok to be closed */
	spin_lock(&endpoint->lock);
	ret = -EBUSY;
	if (endpoint->status != MPOE_ENDPOINT_STATUS_OK) {
		/* only CLOSING and OK endpoints may be attached to the iface */
		BUG_ON(endpoint->status != MPOE_ENDPOINT_STATUS_CLOSING);
		spin_unlock(&endpoint->lock);
		goto out;
	}
	/* mark it as closing so that nobody may use it again */
	endpoint->status = MPOE_ENDPOINT_STATUS_CLOSING;
	/* release our refcount now that other users cannot use again */
	atomic_dec(&endpoint->refcount);
	spin_unlock(&endpoint->lock);

	/* wait until refcount is 0 so that other users are gone */
	add_wait_queue(&endpoint->noref_queue, &wq);
	for(;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!atomic_read(&endpoint->refcount))
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&endpoint->noref_queue, &wq);

	/* release resources */
	mpoe_endpoint_free_resources(endpoint);

	/* detach */
	mpoe_iface_detach_endpoint(endpoint, ifacelocked);

	/* mark as free now */
	endpoint->status = MPOE_ENDPOINT_STATUS_FREE;

	return 0;

 out:
	return ret;
}

static inline int
mpoe_endpoint_close(struct mpoe_endpoint * endpoint)
{
	return __mpoe_endpoint_close(endpoint, 0); /* we don't hold the iface lock */
}

/******************************
 * Acquiring/Releasing endpoints
 */

int
mpoe_endpoint_acquire(struct mpoe_endpoint * endpoint)
{
	int ret = -EINVAL;

	spin_lock(&endpoint->lock);
	if (unlikely(endpoint->status != MPOE_ENDPOINT_STATUS_OK))
		goto out_with_lock;

	atomic_inc(&endpoint->refcount);

	spin_unlock(&endpoint->lock);
	return 0;

 out_with_lock:
	spin_unlock(&endpoint->lock);
	return ret;
}

struct mpoe_endpoint *
mpoe_endpoint_acquire_by_iface_index(struct mpoe_iface * iface, uint8_t index)
{
	struct mpoe_endpoint * endpoint;

	spin_lock(&iface->endpoint_lock);
	if (unlikely(index >= mpoe_endpoint_max))
		goto out_with_iface_lock;

	endpoint = iface->endpoints[index];
	if (unlikely(!endpoint))
		goto out_with_iface_lock;

	spin_lock(&endpoint->lock);
	if (unlikely(endpoint->status != MPOE_ENDPOINT_STATUS_OK))
		goto out_with_endpoint_lock;

	atomic_inc(&endpoint->refcount);

	spin_unlock(&endpoint->lock);
	spin_unlock(&iface->endpoint_lock);
	return endpoint;

 out_with_endpoint_lock:
	spin_unlock(&endpoint->lock);
 out_with_iface_lock:
	spin_unlock(&iface->endpoint_lock);
	return NULL;
}

void
mpoe_endpoint_release(struct mpoe_endpoint * endpoint)
{
	/* decrement refcount and wake up the closer */
	if (unlikely(atomic_dec_and_test(&endpoint->refcount)))
		wake_up(&endpoint->noref_queue);
}

/******************************
 * File operations
 */

static int
mpoe_miscdev_open(struct inode * inode, struct file * file)
{
	struct mpoe_endpoint * endpoint;

	endpoint = kmalloc(sizeof(struct mpoe_endpoint), GFP_KERNEL);
	if (!endpoint)
		return -ENOMEM;

	spin_lock_init(&endpoint->lock);
	endpoint->status = MPOE_ENDPOINT_STATUS_FREE;
	atomic_set(&endpoint->refcount, 0);
	init_waitqueue_head(&endpoint->noref_queue);

	file->private_data = endpoint;
	return 0;
}

static int
mpoe_miscdev_release(struct inode * inode, struct file * file)
{
	struct mpoe_endpoint * endpoint = file->private_data;

	BUG_ON(!endpoint);

	if (endpoint->status != MPOE_ENDPOINT_STATUS_FREE)
		mpoe_endpoint_close(endpoint);

	return 0;
}

/*
 * Common command handlers
 * returns 0 on success, <0 on error,
 * 1 when success and does not want to release the reference on the endpoint
 */
static int (*mpoe_cmd_with_endpoint_handlers[])(struct mpoe_endpoint * endpoint, void __user * uparam) = {
	[MPOE_CMD_SEND_TINY]		= mpoe_send_tiny,
	[MPOE_CMD_SEND_SMALL]		= mpoe_send_small,
	[MPOE_CMD_SEND_MEDIUM]		= mpoe_send_medium,
	[MPOE_CMD_SEND_RENDEZ_VOUS]	= mpoe_send_rendez_vous,
	[MPOE_CMD_SEND_PULL]		= mpoe_send_pull,
	[MPOE_CMD_REGISTER_REGION]	= mpoe_register_user_region,
	[MPOE_CMD_DEREGISTER_REGION]	= mpoe_deregister_user_region,
};

/*
 * Main ioctl switch where all application ioctls arrive
 */
static int
mpoe_miscdev_ioctl(struct inode *inode, struct file *file,
		   unsigned cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {

	case MPOE_CMD_GET_BOARD_COUNT: {
		uint32_t count = mpoe_ifaces_get_count();

		ret = copy_to_user((void __user *) arg, &count,
				   sizeof(count));
		if (ret < 0)
			printk(KERN_ERR "MPoE: Failed to write get_board_count command result, error %d\n", ret);

		break;
	}

	case MPOE_CMD_GET_BOARD_ID: {
		struct mpoe_cmd_get_board_id get_board_id;

		ret = copy_from_user(&get_board_id, (void __user *) arg,
				     sizeof(get_board_id));
		if (ret < 0) {
			printk(KERN_ERR "MPoE: Failed to read get_board_id command argument, error %d\n", ret);
			goto out;
		}

		ret = mpoe_iface_get_id(get_board_id.board_index,
					&get_board_id.board_addr,
					get_board_id.board_name);
		if (ret < 0)
			goto out;

		ret = copy_to_user((void __user *) arg, &get_board_id,
				   sizeof(get_board_id));
		if (ret < 0)
			printk(KERN_ERR "MPoE: Failed to write get_board_id command result, error %d\n", ret);

		break;
	}

	case MPOE_CMD_OPEN_ENDPOINT: {
		struct mpoe_endpoint * endpoint = file->private_data;
		BUG_ON(!endpoint);

		ret = mpoe_endpoint_open(endpoint, (void __user *) arg);

		break;
	}

	case MPOE_CMD_CLOSE_ENDPOINT: {
		struct mpoe_endpoint * endpoint = file->private_data;
		BUG_ON(!endpoint);

		ret = mpoe_endpoint_close(endpoint);

		break;
	}

	case MPOE_CMD_SEND_TINY:
	case MPOE_CMD_SEND_SMALL:
	case MPOE_CMD_SEND_MEDIUM:
	case MPOE_CMD_SEND_RENDEZ_VOUS:
	case MPOE_CMD_SEND_PULL:
	case MPOE_CMD_REGISTER_REGION:
	case MPOE_CMD_DEREGISTER_REGION:
	{
		struct mpoe_endpoint * endpoint = file->private_data;

		BUG_ON(cmd >= ARRAY_SIZE(mpoe_cmd_with_endpoint_handlers));
		BUG_ON(mpoe_cmd_with_endpoint_handlers[cmd] == NULL);

		ret = mpoe_endpoint_acquire(endpoint);
		if (unlikely(ret < 0))
			goto out;

		ret = mpoe_cmd_with_endpoint_handlers[cmd](endpoint, (void __user *) arg);

		/* if ret > 0, the caller wants to keep a reference on the endpoint */
		if (likely(ret <= 0))
			mpoe_endpoint_release(endpoint);

		break;
	}

	default:
		ret = -ENOSYS;
		break;
	}

 out:
	return ret;
}

static int
mpoe_miscdev_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct mpoe_endpoint * endpoint = file->private_data;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;

	if (endpoint == NULL)
		return -EINVAL;

	if (offset == MPOE_SENDQ_FILE_OFFSET && size == MPOE_SENDQ_SIZE)
		return mpoe_remap_vmalloc_range(vma, endpoint->sendq, 0);
	else if (offset == MPOE_RECVQ_FILE_OFFSET && size == MPOE_RECVQ_SIZE)
		return mpoe_remap_vmalloc_range(vma, endpoint->sendq, MPOE_SENDQ_SIZE >> PAGE_SHIFT);
	else if (offset == MPOE_EVENTQ_FILE_OFFSET && size == MPOE_EVENTQ_SIZE)
		return mpoe_remap_vmalloc_range(vma, endpoint->sendq, (MPOE_SENDQ_SIZE + MPOE_RECVQ_SIZE) >> PAGE_SHIFT);
	else {
		printk(KERN_ERR "MPoE: Cannot mmap %lx at %lx\n", size, offset);
		return -EINVAL;
	}
}

static struct file_operations
mpoe_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = mpoe_miscdev_open,
	.release = mpoe_miscdev_release,
	.mmap = mpoe_miscdev_mmap,
	.ioctl = mpoe_miscdev_ioctl,
};

static struct miscdevice
mpoe_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mpoe",
	.fops = &mpoe_miscdev_fops,
};

/******************************
 * Device attributes
 */

#ifdef MPOE_MISCDEV_HAVE_CLASS_DEVICE

static ssize_t
mpoe_ifaces_attr_show(struct class_device *dev, char *buf)
{
	return mpoe_ifaces_show(buf);
}

static ssize_t
mpoe_ifaces_attr_store(struct class_device *dev, const char *buf, size_t size)
{
	return mpoe_ifaces_store(buf, size);
}

static CLASS_DEVICE_ATTR(ifaces, S_IRUGO|S_IWUSR, mpoe_ifaces_attr_show, mpoe_ifaces_attr_store);

static int
mpoe_init_attributes(void)
{
	return class_device_create_file(mpoe_miscdev.class, &class_device_attr_ifaces);
}

static void
mpoe_exit_attributes(void)
{
	class_device_remove_file(mpoe_miscdev.class, &class_device_attr_ifaces);
}

#else /* !MPOE_MISCDEV_HAVE_CLASS_DEVICE */

static ssize_t
mpoe_ifaces_attr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return mpoe_ifaces_show(buf);
}

static ssize_t
mpoe_ifaces_attr_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return mpoe_ifaces_store(buf, size);
}

static DEVICE_ATTR(ifaces, S_IRUGO|S_IWUSR, mpoe_ifaces_attr_show, mpoe_ifaces_attr_store);

static int
mpoe_init_attributes(void)
{
	return device_create_file(mpoe_miscdev.this_device, &dev_attr_ifaces);
}

static void
mpoe_exit_attributes(void)
{
	device_remove_file(mpoe_miscdev.this_device, &dev_attr_ifaces);
}

#endif /* !MPOE_MISCDEV_HAVE_CLASS_DEVICE */


/******************************
 * Device registration
 */

int
mpoe_dev_init(void)
{
	int ret;

	ret = misc_register(&mpoe_miscdev);
	if (ret < 0) {
		printk(KERN_ERR "MPoE: Failed to register misc device, error %d\n", ret);
		goto out;
	}

	ret = mpoe_init_attributes();
	if (ret < 0) {
		printk(KERN_ERR "MPoE: failed to create misc device attributes, error %d\n", ret);
		goto out_with_device;
	}

	return 0;

 out_with_device:
	misc_deregister(&mpoe_miscdev);
 out:
	return ret;
}

void
mpoe_dev_exit(void)
{
	mpoe_exit_attributes();
	misc_deregister(&mpoe_miscdev);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
