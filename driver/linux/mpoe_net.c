#include <linux/kernel.h>
#include <linux/module.h>

#include "mpoe_common.h"
#include "mpoe_hal.h"

/*
 * Scan the list of physical interfaces and return the
 * one that matches ifname (and take a reference on it).
 */
static struct net_device *
dev_hold_by_name(const char * ifname)
{
	struct net_device * ifp;

	read_lock(&dev_base_lock);
	mpoe_for_each_netdev(ifp) {
		dev_hold(ifp);
		if (!strcmp(ifp->name, ifname)) {
			read_unlock(&dev_base_lock);
			return ifp;
		}
		dev_put(ifp);
	}
	read_unlock(&dev_base_lock);

	printk(KERN_ERR "MPoE: Failed to find interface '%s'\n", ifname);
	return NULL;
}

/******************************
 * Managing interfaces
 */

/*
 * Array, number and lock for the list of ifaces
 */
static struct mpoe_iface ** mpoe_ifaces;
static unsigned int mpoe_iface_nr = 0;
static spinlock_t mpoe_iface_lock = SPIN_LOCK_UNLOCKED;

/*
 * Returns the iface associated to a physical interface.
 * Should be used when an incoming packets has been received by ifp.
 */
struct mpoe_iface *
mpoe_iface_find_by_ifp(struct net_device *ifp)
{
	int i;

	/* since iface removal disables incoming packet processing, we don't
	 * need to lock the iface array or to hold a reference on the iface.
	 */
	for (i=0; i<mpoe_iface_max; i++) {
		struct mpoe_iface * iface = mpoe_ifaces[i];
		if (iface && iface->eth_ifp == ifp)
			return iface;
	}

	return NULL;
}

/*
 * Return the number of mpoe ifaces.
 */
int
mpoe_ifaces_get_count(void)
{
	int i, count = 0;

	/* no need to lock since the array of iface is always coherent
	 * and we don't access the internals of the ifaces
	 */
	for (i=0; i<mpoe_iface_max; i++)
		if (mpoe_ifaces[i] != NULL)
			count++;

	return count;
}

/*
 * Return the address and name of an iface.
 */
int
mpoe_iface_get_id(uint8_t board_index, struct mpoe_mac_addr * board_addr, char * board_name)
{
	struct net_device * ifp;
	int ret;

	/* need to lock since we access the internals of the iface */
	spin_lock(&mpoe_iface_lock);

	ret = -EINVAL;
	if (board_index >= mpoe_iface_max
	    || mpoe_ifaces[board_index] == NULL)
		goto out_with_lock;

	ifp = mpoe_ifaces[board_index]->eth_ifp;

	mpoe_mac_addr_of_netdevice(ifp, board_addr);
	strncpy(board_name, ifp->name, MPOE_IF_NAMESIZE);

	spin_unlock(&mpoe_iface_lock);

	return 0;

 out_with_lock:
	spin_unlock(&mpoe_iface_lock);
	return ret;
}

/******************************
 * Attaching/Detaching interfaces
 */

/*
 * Attach a new iface.
 *
 * Must be called with ifaces lock hold.
 */
static int
mpoe_iface_attach(struct net_device * ifp)
{
	struct mpoe_iface * iface;
	int ret;
	int i;

	if (mpoe_iface_nr == mpoe_iface_max) {
		printk(KERN_ERR "MPoE: Too many interfaces already attached\n");
		ret = -EBUSY;
		goto out_with_ifp_hold;
	}

	if (mpoe_iface_find_by_ifp(ifp)) {
		printk(KERN_ERR "MPoE: Interface %s already attached\n", ifp->name);
		ret = -EBUSY;
		goto out_with_ifp_hold;
	}

	for(i=0; i<mpoe_iface_max; i++)
		if (mpoe_ifaces[i] == NULL)
			break;

	iface = kzalloc(sizeof(struct mpoe_iface), GFP_KERNEL);
	if (!iface) {
		printk(KERN_ERR "MPoE: Failed to allocate interface as board %d\n", i);
		ret = -ENOMEM;
		goto out_with_ifp_hold;
	}

	printk(KERN_INFO "MPoE: Attaching interface '%s' as #%i\n", ifp->name, i);

	iface->eth_ifp = ifp;
	iface->endpoint_nr = 0;
	iface->endpoints = kzalloc(mpoe_endpoint_max * sizeof(struct mpoe_endpoint *), GFP_KERNEL);
	if (!iface->endpoints) {
		printk(KERN_ERR "MPoE: Failed to allocate interface endpoint pointers\n");
		ret = -ENOMEM;
		goto out_with_iface;
	}

	init_waitqueue_head(&iface->noendpoint_queue);
	spin_lock_init(&iface->endpoint_lock);
	iface->index = i;
	mpoe_iface_nr++;
	mpoe_ifaces[i] = iface;

	return 0;

 out_with_iface:
	kfree(iface);
 out_with_ifp_hold:
	return ret;
}

/*
 * Detach an existing iface, possibly by force.
 *
 * Must be called with ifaces lock hold.
 * Incoming packets should be disabled (by temporarily
 * removing mpoe_pt in the caller if necessary)
 * to prevent users while detaching the iface.
 */
static int
__mpoe_iface_detach(struct mpoe_iface * iface, int force)
{
	DECLARE_WAITQUEUE(wq, current);
	int ret;
	int i;

	BUG_ON(mpoe_ifaces[iface->index] == NULL);

	/* mark as closing so that nobody opens a new endpoint,
	 * protected by the ifaces lock
	 */
	iface->status = MPOE_IFACE_STATUS_CLOSING;

	/* if force, close all endpoints.
	 * if not force, error if some endpoints are open.
	 */
	spin_lock(&iface->endpoint_lock);
	ret = -EBUSY;
	if (!force && iface->endpoint_nr) {
		printk(KERN_INFO "MPoE: cannot detach interface #%d '%s', still %d endpoints open\n",
		       iface->index, iface->eth_ifp->name, iface->endpoint_nr);
		spin_unlock(&iface->endpoint_lock);
		goto out;
	}

	for(i=0; i<mpoe_endpoint_max; i++) {
		struct mpoe_endpoint * endpoint = iface->endpoints[i];
		if (!endpoint)
			continue;

		printk(KERN_INFO "MPoE: forcing close of endpoint #%d attached to iface #%d '%s'\n",
		       i, iface->index, iface->eth_ifp->name);

		/* close the endpoint, with the iface lock hold */
		ret = __mpoe_endpoint_close(endpoint, 1);
		if (ret < 0) {
			BUG_ON(ret != -EBUSY);
			/* somebody else is already closing this endpoint,
			 * let's forget about it for now, we'll wait later
			 */
		}
	}

	/* wait for concurrent endpoint closers to be done */
	add_wait_queue(&iface->noendpoint_queue, &wq);
	for(;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!iface->endpoint_nr)
			break;
		spin_unlock(&iface->endpoint_lock);
		schedule();
		spin_lock(&iface->endpoint_lock);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&iface->noendpoint_queue, &wq);
	spin_unlock(&iface->endpoint_lock);

	printk(KERN_INFO "MPoE: detaching interface #%d '%s'\n", iface->index, iface->eth_ifp->name);

	mpoe_ifaces[iface->index] = NULL;
	mpoe_iface_nr--;
	kfree(iface->endpoints);
	kfree(iface);

	return 0;

 out:
	return ret;
}

static inline int
mpoe_iface_detach(struct mpoe_iface * iface)
{
	return __mpoe_iface_detach(iface, 0);
}

static inline int
mpoe_iface_detach_force(struct mpoe_iface * iface)
{
	return __mpoe_iface_detach(iface, 1);
}

/******************************
 * Attribute-based attaching/detaching of interfaces
 */

/*
 * Format a buffer containing the list of attached ifaces.
 */
int
mpoe_ifaces_show(char *buf)
{
	int total = 0;
	int i;

	/* need to lock since we access the internals of the ifaces */
	spin_lock(&mpoe_iface_lock);

	for (i=0; i<mpoe_iface_max; i++) {
		struct mpoe_iface * iface = mpoe_ifaces[i];
		if (iface) {
			char * ifname = iface->eth_ifp->name;
			int length = strlen(ifname);
			/* FIXME: check total+length+2 <= PAGE_SIZE ? */
			strcpy(buf, ifname);
			buf += length;
			strcpy(buf, "\n");
			buf += 1;
			total += length+1;
		}
	}

	spin_unlock(&mpoe_iface_lock);

	return total + 1;
}

/*
 * Attach/Detach ifaces depending on the given string.
 *
 * +name adds an iface
 * -name removes one
 */
int
mpoe_ifaces_store(const char *buf, size_t size)
{
	char copy[IFNAMSIZ];
	char * ptr;

	/* remove the ending \n if required, so copy first since buf is const */
	strncpy(copy, buf+1, IFNAMSIZ);
	copy[IFNAMSIZ-1] = '\0';
	ptr = strchr(copy, '\n');
	if (ptr)
		*ptr = '\0';

	if (buf[0] == '-') {
		int i;
		/* if none matches, we return -EINVAL.
		 * if one matches, it sets ret accordingly.
		 */
		int ret = -EINVAL;

		spin_lock(&mpoe_iface_lock);
		for(i=0; i<mpoe_iface_max; i++) {
			struct mpoe_iface * iface = mpoe_ifaces[i];
			struct net_device * ifp;

			if (iface == NULL)
				continue;

			ifp = iface->eth_ifp;
			if (strcmp(ifp->name, copy))
				continue;

			/* disable incoming packets while removing the iface
			 * to prevent races
			 */
			dev_remove_pack(&mpoe_pt);
			ret = mpoe_iface_detach(iface);
			dev_add_pack(&mpoe_pt);

			/* release the interface now */
			dev_put(ifp);
			break;
		}
		spin_unlock(&mpoe_iface_lock);

		if (ret == -EINVAL) {
			printk(KERN_ERR "MPoE: Cannot find any attached interface '%s' to detach\n", copy);
			return -EINVAL;
		}
		return size;

	} else if (buf[0] == '+') {
		struct net_device * ifp;
		int ret;

		ifp = dev_hold_by_name(copy);
		if (!ifp)
			return -EINVAL;

		spin_lock(&mpoe_iface_lock);
		ret = mpoe_iface_attach(ifp);
		spin_unlock(&mpoe_iface_lock);
		if (ret < 0) {
			dev_put(ifp);
			return ret;
		}

		return size;

	} else {
		printk(KERN_ERR "MPoE: Unrecognized command passed in the ifaces file, need either +name or -name\n");
		return -EINVAL;
	}
}

/******************************
 * Attaching/Detaching endpoints to ifaces
 */

/*
 * Attach a new endpoint
 */
int
mpoe_iface_attach_endpoint(struct mpoe_endpoint * endpoint)
{
	struct mpoe_iface * iface;
	int ret;

	BUG_ON(endpoint->status != MPOE_ENDPOINT_STATUS_INITIALIZING);

	ret = -EINVAL;
	if (endpoint->endpoint_index >= mpoe_endpoint_max)
		goto out;

	/* lock the list of ifaces */
	spin_lock(&mpoe_iface_lock);

	/* find the iface */
	ret = -EINVAL;
	if (endpoint->board_index >= mpoe_iface_max
	    || (iface = mpoe_ifaces[endpoint->board_index]) == NULL
	    || iface->status != MPOE_IFACE_STATUS_OK) {
		printk(KERN_ERR "MPoE: Cannot open endpoint on unexisting board %d\n",
		       endpoint->board_index);
		goto out_with_ifaces_locked;
	}
	iface = mpoe_ifaces[endpoint->board_index];

	/* lock the list of endpoints in the iface */
	spin_lock(&iface->endpoint_lock);

	/* add the endpoint */
	if (iface->endpoints[endpoint->endpoint_index] != NULL) {
		printk(KERN_ERR "MPoE: endpoint already open\n");
		goto out_with_endpoints_locked;
	}

	iface->endpoints[endpoint->endpoint_index] = endpoint ;
	iface->endpoint_nr++;
	endpoint->iface = iface;

	/* mark the endpoint as open here so that anybody removing this
	 * iface never sees any endpoint in status INIT in the iface list
	 * (only OK and CLOSING are allowed there)
	 */
	endpoint->status = MPOE_ENDPOINT_STATUS_OK;

	spin_unlock(&iface->endpoint_lock);
	spin_unlock(&mpoe_iface_lock);

	return 0;

 out_with_endpoints_locked:
	spin_unlock(&iface->endpoint_lock);
 out_with_ifaces_locked:
	spin_unlock(&mpoe_iface_lock);
 out:
	return ret;
}

/*
 * Detach an existing endpoint
 *
 * Must be called while endpoint is status CLOSING.
 *
 * ifacelocked is set when detaching an iface and thus removing all endpoints
 * by force.
 * It is not (and thus the iface lock has to be taken) when the endpoint is
 * normally closed from the application.
 */
void
mpoe_iface_detach_endpoint(struct mpoe_endpoint * endpoint,
			   int ifacelocked)
{
	struct mpoe_iface * iface = endpoint->iface;

	BUG_ON(endpoint->status != MPOE_ENDPOINT_STATUS_CLOSING);

	/* lock the list of endpoints in the iface, if needed */
	if (!ifacelocked)
		spin_lock(&iface->endpoint_lock);

	BUG_ON(iface->endpoints[endpoint->endpoint_index] != endpoint);
	iface->endpoints[endpoint->endpoint_index] = NULL;
	/* decrease the number of endpoints and wakeup the iface detacher if needed */
	if (!--iface->endpoint_nr)
		wake_up(&iface->noendpoint_queue);

	if (!ifacelocked)
		spin_unlock(&iface->endpoint_lock);
}

/******************************
 * Netdevice notifier
 */

static int
mpoe_netdevice_notifier_cb(struct notifier_block *unused,
			   unsigned long event, void *ptr)
{
	struct net_device *ifp = (struct net_device *) ptr;

	if (event == NETDEV_UNREGISTER) {
		struct mpoe_iface * iface;

		spin_lock(&mpoe_iface_lock);
		iface = mpoe_iface_find_by_ifp(ifp);
		if (iface) {
			int ret;
			printk(KERN_INFO "MPoE: interface '%s' being unregistered, forcing closing of endpoints...\n",
			       ifp->name);
			/* there is no need to disable incoming packets since
			 * the ethernet ifp is already disabled before the notifier is called
			 */
			ret = mpoe_iface_detach_force(iface);
			BUG_ON(ret);
			dev_put(ifp);
		}
		spin_unlock(&mpoe_iface_lock);
	}

	return NOTIFY_DONE;
}

static struct notifier_block mpoe_netdevice_notifier = {
	.notifier_call = mpoe_netdevice_notifier_cb,
};

/******************************
 * Initialization and termination
 */

int
mpoe_net_init(const char * ifnames)
{
	int ret = 0;

	dev_add_pack(&mpoe_pt);

	ret = register_netdevice_notifier(&mpoe_netdevice_notifier);
	if (ret < 0) {
		printk(KERN_ERR "MPoE: failed to register netdevice notifier\n");
		goto out_with_pack;
	}

	mpoe_ifaces = kzalloc(mpoe_iface_max * sizeof(struct mpoe_iface *), GFP_KERNEL);
	if (!mpoe_ifaces) {
		printk(KERN_ERR "MPoE: failed to allocate interface array\n");
		ret = -ENOMEM;
		goto out_with_notifier;
	}

	if (ifnames) {
		/* attach ifaces whose name are in ifnames (limited to mpoe_iface_max) */
		char * copy = kstrdup(ifnames, GFP_KERNEL);
		char * ifname;

		while ((ifname = strsep(&copy, ",")) != NULL) {
			struct net_device * ifp;
			ifp = dev_hold_by_name(ifname);
			if (ifp)
				if (mpoe_iface_attach(ifp) < 0) {
					dev_put(ifp);
					break;
				}
		}

		kfree(copy);

	} else {
		/* attach everything (limited to mpoe_iface_max) */
		struct net_device * ifp;

		read_lock(&dev_base_lock);
		mpoe_for_each_netdev(ifp) {
			dev_hold(ifp);
			if (mpoe_iface_attach(ifp) < 0) {
				dev_put(ifp);
				break;
			}
		}
		read_unlock(&dev_base_lock);
	}

	printk(KERN_INFO "MPoE: attached %d interfaces\n", mpoe_iface_nr);
	return 0;

 out_with_notifier:
	unregister_netdevice_notifier(&mpoe_netdevice_notifier);
 out_with_pack:
	dev_remove_pack(&mpoe_pt);
	return ret;
}

void
mpoe_net_exit(void)
{
	int i, nr = 0;

	/* module unloading cannot happen before all users exit
	 * since they hold a reference on the chardev.
	 * so all endpoints are closed once we arrive here.
	 */

	dev_remove_pack(&mpoe_pt);
	/* now, no iface may be used by any incoming packet */

	/* prevent mpoe_netdevice_notifier from removing an iface now */
	spin_lock(&mpoe_iface_lock);

	for (i=0; i<mpoe_iface_max; i++) {
		struct mpoe_iface * iface = mpoe_ifaces[i];
		if (iface != NULL) {
			struct net_device * ifp = iface->eth_ifp;

			/* detach the iface now.
			 * all endpoints are closed, no need to force
			 */
			BUG_ON(mpoe_iface_detach(iface) < 0);
			dev_put(ifp);
			nr++;
		}
	}
	printk(KERN_INFO "MPoE: detached %d interfaces\n", nr);

	/* release the lock to let mpoe_netdevice_notifier finish
	 * in case it has been invoked during our loop
	 */
	spin_unlock(&mpoe_iface_lock);
	/* unregister the notifier then */
	unregister_netdevice_notifier(&mpoe_netdevice_notifier);

	/* free structures now that the notifier is gone */
	kfree(mpoe_ifaces);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
