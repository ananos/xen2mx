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
#include <linux/utsname.h>
#include <linux/if_arp.h>
#include <linux/rcupdate.h>
#include <linux/ethtool.h>
#include <linux/device.h>
#include <linux/pci.h>
#ifdef OMX_HAVE_MUTEX
#include <linux/mutex.h>
#endif

#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_endpoint.h"

/* forward declaration */
static void __omx_iface_last_release(struct kref *kref);

/******************************
 * Managing interfaces
 */

/*
 * Array, number and lock for the list of ifaces
 */
static struct omx_iface ** omx_ifaces = NULL; /* must be NULL during init so that module param are delayed */
static unsigned omx_iface_nr = 0;
static struct mutex omx_ifaces_mutex;
struct omx_iface * omx_shared_fake_iface = NULL; /* only used for shared communication counters */

/*
 * Lock/Unlock the ifaces array
 */
void
omx_ifaces_lock(void)
{
	mutex_lock(&omx_ifaces_mutex);
}

void
omx_ifaces_unlock(void)
{
	mutex_unlock(&omx_ifaces_mutex);
}

/*
 * Return an iface and keep the ifaces lock on success
 */
struct omx_iface *
omx_iface_find_by_index_lock(int board_index)
{
	struct omx_iface * iface;

	mutex_lock(&omx_ifaces_mutex);

	if (board_index >= omx_iface_max) {
		mutex_unlock(&omx_ifaces_mutex);
		return NULL;
	}

	iface = omx_ifaces[board_index];
	if (!iface)
		mutex_unlock(&omx_ifaces_mutex);

	return iface;
}

/*
 * Returns the iface associated to a physical interface.
 * Should be used when an incoming packets has been received by ifp.
 */
struct omx_iface *
omx_iface_find_by_ifp(struct net_device *ifp)
{
	int i;

	/* since iface removal disables incoming packet processing, we don't
	 * need to lock the iface array or to hold a reference on the iface.
	 */
	for (i=0; i<omx_iface_max; i++) {
		struct omx_iface * iface = omx_ifaces[i];
		if (likely(iface && iface->eth_ifp == ifp))
			return iface;
	}

	return NULL;
}

/*
 * Returns the iface associated with an address.
 * Used by the peer table, which needs a reference on the iface.
 */
struct omx_iface *
omx_iface_find_by_addr(uint64_t addr)
{
	int i;

	rcu_read_lock();

	for (i=0; i<omx_iface_max; i++) {
		struct omx_iface * iface = rcu_dereference(omx_ifaces[i]);
		if (unlikely(iface && iface->peer.board_addr == addr)) {
			omx_iface_reacquire(iface);
			rcu_read_unlock();
			return iface;
		}
	}

	rcu_read_unlock();

	return NULL;
}

/*
 * Return the number of omx ifaces.
 */
int
omx_ifaces_get_count(void)
{
	int i, count = 0;

	/* no need to lock since the array of iface is always coherent
	 * and we don't access the internals of the ifaces
	 */
	for (i=0; i<omx_iface_max; i++)
		if (omx_ifaces[i] != NULL)
			count++;

	return count;
}

/*
 * Return the address and name of an iface.
 */
int
omx_iface_get_info(uint32_t board_index, struct omx_board_info *info)
{
	struct omx_iface * iface;
	struct net_device * ifp;
	int ret;

	rcu_read_lock();

	if (board_index == OMX_SHARED_FAKE_IFACE_INDEX) {
		info->addr = 0;
		info->numa_node = -1;
		strncpy(info->ifacename, "fake", OMX_IF_NAMESIZE);
		info->ifacename[OMX_IF_NAMESIZE-1] = '\0';
		strncpy(info->hostname, "Shared Communication", OMX_HOSTNAMELEN_MAX);
		info->hostname[OMX_HOSTNAMELEN_MAX-1] = '\0';

	} else {
		ret = -EINVAL;
		if (board_index >= omx_iface_max)
			goto out_with_lock;

		iface = rcu_dereference(omx_ifaces[board_index]);
		if (!iface)
			goto out_with_lock;

		ifp = iface->eth_ifp;

		info->addr = iface->peer.board_addr;
		info->numa_node = omx_ifp_node(iface->eth_ifp);
		strncpy(info->ifacename, ifp->name, OMX_IF_NAMESIZE);
		info->ifacename[OMX_IF_NAMESIZE-1] = '\0';
		strncpy(info->hostname, iface->peer.hostname, OMX_HOSTNAMELEN_MAX);
		info->hostname[OMX_HOSTNAMELEN_MAX-1] = '\0';
	}

	rcu_read_unlock();
	return 0;

 out_with_lock:
	rcu_read_unlock();
	return ret;
}

int
omx_iface_get_counters(uint32_t board_index, int clear,
		       uint64_t buffer_addr, uint32_t buffer_length)
{
	struct omx_iface * iface;
	int ret;

	rcu_read_lock();

	if (board_index == OMX_SHARED_FAKE_IFACE_INDEX) {
		iface = omx_shared_fake_iface;

	} else {
		ret = -EINVAL;
		if (board_index >= omx_iface_max)
			goto out_with_lock;

		iface = rcu_dereference(omx_ifaces[board_index]);
		if (!iface)	
			goto out_with_lock;
	}

	if (buffer_length < sizeof(iface->counters))
		buffer_length = sizeof(iface->counters);

	ret = copy_to_user((void __user *) (unsigned long) buffer_addr, iface->counters,
			   buffer_length);
	if (unlikely(ret != 0))
		ret = -EFAULT;

	if (clear)
		memset(iface->counters, 0, sizeof(iface->counters));

 out_with_lock:
	rcu_read_unlock();
	return ret;
}

int
omx_iface_set_hostname(uint32_t board_index, char * hostname)
{
	struct omx_iface * iface;
	char * new_hostname, * old_hostname;
	int ret;

	new_hostname = kstrdup(hostname, GFP_KERNEL);
	if (!new_hostname) {
		printk(KERN_ERR "Open-MX: failed to allocate the new hostname string\n");
		ret = -ENOMEM;
		goto out;
	}

	rcu_read_lock();

	ret = -EINVAL;
	if (board_index >= omx_iface_max)
		goto out_with_lock;

	iface = rcu_dereference(omx_ifaces[board_index]);
	if (!iface)
		goto out_with_lock;

	printk(KERN_INFO "Open-MX: changing board %d (interface '%s') hostname from %s to %s\n",
	       board_index, iface->eth_ifp->name, iface->peer.hostname, hostname);

	old_hostname = iface->peer.hostname;
	iface->peer.hostname = new_hostname;
	kfree(old_hostname);

	/* FIXME: update peer table */

	rcu_read_unlock();
	return 0;

 out_with_lock:
	rcu_read_unlock();
	kfree(new_hostname);
 out:
	return ret;
}

void
omx_iface_release(struct omx_iface * iface)
{
	kref_put(&iface->refcount, __omx_iface_last_release);
}

void
omx_for_each_iface(int (*handler)(struct omx_iface *iface, void *data), void *data)
{
	int i;

	rcu_read_lock();
	for(i=0; i<omx_iface_max; i++) {
		struct omx_iface *iface = rcu_dereference(omx_ifaces[i]);
		if (!iface)
			continue;
		if (handler(iface, data) < 0)
			break;
	}
	rcu_read_unlock();
}

void
omx_for_each_endpoint(int (*handler)(struct omx_endpoint *endpoint, void *data), void *data)
{
	int i,j;

	rcu_read_lock();
	for(i=0; i<omx_iface_max; i++) {
		struct omx_iface *iface = rcu_dereference(omx_ifaces[i]);
		if (!iface)
			continue;

		for(j=0; j<omx_endpoint_max; j++) {
			struct omx_endpoint *endpoint = rcu_dereference(iface->endpoints[j]);
			if (!endpoint)
				continue;
			if (handler(endpoint, data) < 0)
				break;
		}
	}
	rcu_read_unlock();
}

void
omx_for_each_endpoint_in_mm(struct mm_struct *mm,
			    int (*handler)(struct omx_endpoint *endpoint, void *data), void *data)
{
	int i,j;

	rcu_read_lock();
	for(i=0; i<omx_iface_max; i++) {
		struct omx_iface *iface = rcu_dereference(omx_ifaces[i]);
		if (!iface)
			continue;

		for(j=0; j<omx_endpoint_max; j++) {
			struct omx_endpoint *endpoint = rcu_dereference(iface->endpoints[j]);
			if (!endpoint || endpoint->opener_mm != mm)
				continue;
			if (handler(endpoint, data) < 0)
				break;
		}
	}
	rcu_read_unlock();
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
omx_iface_attach(struct net_device * ifp)
{
	struct omx_iface * iface;
	struct device *dev;
	char *hostname;
	unsigned mtu = ifp->mtu;
	int ret;
	int i;

	if (omx_iface_nr == omx_iface_max) {
		printk(KERN_ERR "Open-MX: Too many interfaces already attached\n");
		ret = -EBUSY;
		goto out_with_ifp_hold;
	}

	if (omx_iface_find_by_ifp(ifp)) {
		printk(KERN_ERR "Open-MX: Interface '%s' already attached\n", ifp->name);
		ret = -EBUSY;
		goto out_with_ifp_hold;
	}

	for(i=0; i<omx_iface_max; i++)
		if (omx_ifaces[i] == NULL)
			break;

	iface = kzalloc(sizeof(struct omx_iface), GFP_KERNEL);
	if (!iface) {
		printk(KERN_ERR "Open-MX: Failed to allocate interface as board %d\n", i);
		ret = -ENOMEM;
		goto out_with_ifp_hold;
	}

	printk(KERN_INFO "Open-MX: Attaching %sEthernet interface '%s' as #%i, MTU=%d\n",
	       (ifp->type == ARPHRD_ETHER ? "" : "non-"), ifp->name, i, mtu);

	dev = omx_ifp_to_dev(ifp);
#ifdef CONFIG_PCI
	if (dev && dev->bus == &pci_bus_type) {
		struct pci_dev *pdev = to_pci_dev(dev);
		char *symbol_name;

		BUG_ON(!pdev->driver);
		printk(KERN_INFO "Open-MX:   Interface '%s' is PCI device '%s' managed by driver '%s'\n",
		       ifp->name, dev->bus_id, pdev->driver->name);

		symbol_name = kmalloc(sizeof(pdev->driver->name)
				      +sizeof("_get_omx_endpoint_irq")+1, GFP_KERNEL);
		if (symbol_name) {
			sprintf(symbol_name, "%s_get_omx_endpoint_irq", pdev->driver->name);
			iface->get_endpoint_irq_symbol_name = symbol_name;
		}
	}
#endif

	if (!(dev_get_flags(ifp) & IFF_UP))
		printk(KERN_WARNING "Open-MX:   WARNING: Interface '%s' is not up\n",
		       ifp->name);
	if (mtu < OMX_MTU)
		printk(KERN_WARNING "Open-MX:   WARNING: Interface '%s' MTU should be at least %d, current value %d might cause problems\n",
		       ifp->name, OMX_MTU, mtu);

	if (ifp->ethtool_ops && ifp->ethtool_ops->get_coalesce) {
		struct ethtool_coalesce coal;
		ifp->ethtool_ops->get_coalesce(ifp, &coal);
		if (coal.rx_coalesce_usecs >= OMX_IFACE_RX_USECS_WARN_MIN)
			printk(KERN_WARNING "Open-MX:   WARNING: Interface '%s' interrupt coalescing very high (%ldus)\n",
			       ifp->name, (unsigned long) coal.rx_coalesce_usecs);
	}

	hostname = kmalloc(OMX_HOSTNAMELEN_MAX, GFP_KERNEL);
	if (!hostname) {
		printk(KERN_ERR "Open-MX:   Failed to allocate interface hostname\n");
		ret = -ENOMEM;
		goto out_with_iface;
	}

	snprintf(hostname, OMX_HOSTNAMELEN_MAX, "%s:%d", omx_current_utsname.nodename, i);
	hostname[OMX_HOSTNAMELEN_MAX-1] = '\0';
	iface->peer.hostname = hostname;
	iface->peer.index = OMX_UNKNOWN_REVERSE_PEER_INDEX;
	iface->peer.reverse_index = OMX_UNKNOWN_REVERSE_PEER_INDEX;
	iface->peer.board_addr = omx_board_addr_from_netdevice(ifp);

	iface->eth_ifp = ifp;
	iface->endpoint_nr = 0;
	iface->endpoints = kzalloc(omx_endpoint_max * sizeof(struct omx_endpoint *), GFP_KERNEL);
	if (!iface->endpoints) {
		printk(KERN_ERR "Open-MX:   Failed to allocate interface endpoint pointers\n");
		ret = -ENOMEM;
		goto out_with_iface_hostname;
	}

	omx_iface_raw_init(&iface->raw);

	kref_init(&iface->refcount);
	mutex_init(&iface->endpoints_mutex);

	/* insert in the peer table */
	omx_peers_notify_iface_attach(iface);

	iface->index = i;
	omx_iface_nr++;
	rcu_assign_pointer(omx_ifaces[i], iface);

	return 0;

 out_with_iface_hostname:
	kfree(hostname);
 out_with_iface:
	kfree(iface);
 out_with_ifp_hold:
	return ret;
}

/* Called when the last reference on the iface is released */
static void
__omx_iface_last_release(struct kref *kref)
{
	struct omx_iface * iface = container_of(kref, struct omx_iface, refcount);
	struct net_device * ifp = iface->eth_ifp;

	dprintk(KREF, "releasing the last reference on %s (interface '%s')\n",
		iface->peer.hostname, ifp->name);

	omx_iface_raw_exit(&iface->raw);
	kfree(iface->endpoints);
	kfree(iface->peer.hostname);
	kfree(iface);

	/* release the interface now, it will wakeup the unregister notifier waiting in rtnl_unlock() */
	dev_put(ifp);
}

/*
 * Detach an existing iface, possibly by force.
 *
 * Must be called with ifaces lock hold.
 * Incoming packets should be disabled (by temporarily
 * removing omx_pt in the caller if necessary)
 * to prevent users while detaching the iface.
 */
static int
omx_iface_detach(struct omx_iface * iface, int force)
{
	int ret;
	int i;

	BUG_ON(omx_ifaces[iface->index] == NULL);

	/* take the lock before changing/restoring the status to support concurrent tries */
	mutex_lock(&iface->endpoints_mutex);

	/* if force, close all endpoints.
	 * if not force, error if some endpoints are open.
	 */
	ret = -EBUSY;
	if (!force && iface->endpoint_nr) {
		printk(KERN_INFO "Open-MX: cannot detach interface '%s' (#%d), still %d endpoints open\n",
		       iface->eth_ifp->name, iface->index, iface->endpoint_nr);
		mutex_unlock(&iface->endpoints_mutex);
		goto out;
	}

	/*
	 * Detach is guaranteed to succeed now, we can mark the iface as closing.
	 * The ifaces lock is protecting us from concurrent accesses.
	 * Nobody will be able to open a new endpoint.
	 */
	iface->status = OMX_IFACE_STATUS_CLOSING;

	for(i=0; i<omx_endpoint_max; i++) {
		struct omx_endpoint * endpoint = iface->endpoints[i];
		if (!endpoint)
			continue;

		printk(KERN_INFO "Open-MX: forcing close of endpoint #%d attached to interface '%s' (#%d)\n",
		       i, iface->eth_ifp->name, iface->index);

		/* notify the interface removal to userspace */
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_IFACE_REMOVED;
		/*
		 * schedule the endpoint closing, with the iface lock hold
		 * ignore the return value, somebody might be closing it already
		 */
		omx_endpoint_close(endpoint, 1);
		/*
		 * no need to wait for anything, the last endpoint reference
		 * will release the iface, the last iface reference will
		 * release the device and wake up unregister_netdevice()
		 */
	}

	mutex_unlock(&iface->endpoints_mutex);

	printk(KERN_INFO "Open-MX: Detaching interface '%s' (#%d)\n",
	       iface->eth_ifp->name, iface->index);

	/* detach the raw */
	omx__raw_detach_iface_locked(iface);

	/* remove from the peer table */
	omx_peers_notify_iface_detach(iface);

	/* remove the iface from the array */
	rcu_assign_pointer(omx_ifaces[iface->index], NULL);
	omx_iface_nr--;
	/* no need to bother using call_rcu() here, waiting a bit long in synchronize_rcu() is ok */
	synchronize_rcu();

	/* let the last reference release the iface's internals */
	omx_iface_release(iface);

	return 0;

 out:
	return ret;
}

/****************************************************
 * Attribute-based attaching/detaching of interfaces
 */

/*
 * Format a buffer containing the list of attached ifaces.
 */
int
omx_ifnames_get(char *buf, struct kernel_param *kp)
{
	int total = 0;
	int i;

	rcu_read_lock();

	for (i=0; i<omx_iface_max; i++) {
		struct omx_iface * iface = rcu_dereference(omx_ifaces[i]);
		if (iface) {
			char * ifname = iface->eth_ifp->name;
			int length = strlen(ifname);
			if (total + length + 2 > PAGE_SIZE) {
				printk(KERN_ERR "Open-MX: Failed to get all interface names within a single page, ignoring the last ones\n");
				break;
			}
			strcpy(buf, ifname);
			buf += length;
			strcpy(buf, "\n");
			buf += 1;
			total += length+1;
		}
	}

	rcu_read_unlock();

	return total + 1;
}

/*
 * Attach/Detach one iface depending on the given string.
 *
 * name or +name adds an iface
 * -name removes one
 * --name removes one by force, even if some endpoints are open
 */
static int
omx_ifaces_store_one(const char *buf)
{
	int ret = 0;

	if (buf[0] == '-') {
		const char *ifname;
		int force = 0;
		int i;

		/* if none matches, we return -EINVAL.
		 * if one matches, it sets ret accordingly.
		 */
		ret = -EINVAL;

		/* remove the first '-' and possibly another one for force */
		ifname = buf+1;
		if (ifname[0] == '-') {
			ifname++;
			force = 1;
		}

		mutex_lock(&omx_ifaces_mutex);
		for(i=0; i<omx_iface_max; i++) {
			struct omx_iface * iface = omx_ifaces[i];
			struct net_device * ifp;

			if (iface == NULL)
				continue;

			ifp = iface->eth_ifp;
			if (strcmp(ifp->name, ifname))
				continue;

			/*
			 * disable incoming packets while removing the iface
			 * to prevent races
			 */
			dev_remove_pack(&omx_pt);
			/*
			 * no new packets will be received now,
			 * and all the former are already done
			 */
			ret = omx_iface_detach(iface, force);
			dev_add_pack(&omx_pt);

			break;
		}
		mutex_unlock(&omx_ifaces_mutex);

		if (ret == -EINVAL)
			printk(KERN_ERR "Open-MX: Cannot find any attached interface '%s' to detach\n",
			       ifname);

	} else {
		const char *ifname = buf;
		struct net_device * ifp;

		/* remove the first optional '+' */
		if (buf[0] == '+')
			ifname++;

		ifp = omx_dev_get_by_name(ifname);
		if (ifp) {
			mutex_lock(&omx_ifaces_mutex);
			ret = omx_iface_attach(ifp);
			mutex_unlock(&omx_ifaces_mutex);
			if (ret < 0)
				dev_put(ifp);
		} else {
			printk(KERN_ERR "Open-MX: Cannot find interface '%s' to attach\n",
			       ifname);
		}
	}

	return ret;
}

/*
 * Attach/Detach one or multiple ifaces depending on the given string,
 * comma- or \n-separated, and \0-terminated.
 */
static void
omx_ifaces_store(const char *buf)
{
	const char *ptr = buf;

	while (1) {
		char tmp[IFNAMSIZ+2];
		size_t len;

		len = strcspn(ptr, ",\n\0");
		if (!len)
			goto next;
		if (len >= sizeof(tmp))
			goto next;

		/* copy the word in tmp, and keep one byte to add the ending \0 */
		strncpy(tmp, ptr, len);
		tmp[len] = '\0';
		omx_ifaces_store_one(tmp);

	next:
		ptr += len;
		if (*ptr == '\0')
			break;
		ptr++;
	}
}

static char *omx_delayed_ifnames = NULL;

int
omx_ifnames_set(const char *buf, struct kernel_param *kp)
{
	if (omx_ifaces) {
		/* module parameter values are guaranteed to be \0-terminated */
		omx_ifaces_store(buf);
	} else {
		/* the module init isn't done yet, let the string be parsed later */
		omx_delayed_ifnames = kstrdup(buf, GFP_KERNEL);
	}
	return 0;
}

/******************************
 * Attaching/Detaching endpoints to ifaces
 */

/*
 * Attach a new endpoint
 */
int
omx_iface_attach_endpoint(struct omx_endpoint * endpoint)
{
	struct omx_iface * iface;
	int ret;

	BUG_ON(endpoint->status != OMX_ENDPOINT_STATUS_INITIALIZING);

	ret = -EINVAL;
	if (endpoint->endpoint_index >= omx_endpoint_max)
		goto out;

	/* find the iface */
	ret = -EINVAL;
	if (endpoint->board_index >= omx_iface_max)
		goto out;

	rcu_read_lock();

	iface = rcu_dereference(omx_ifaces[endpoint->board_index]);

	ret = -ENODEV;
	if (!iface || iface->status != OMX_IFACE_STATUS_OK) {
		dprintk(IOCTL, "cannot open endpoint on unexisting board %d\n",
		       endpoint->board_index);
		goto out_with_rcu_lock;
	}

	/* take a reference on the iface and release the lock */
	omx_iface_reacquire(iface);
	rcu_read_unlock();

	/* lock the list of endpoints in the iface */
	mutex_lock(&iface->endpoints_mutex);

	/* add the endpoint */
	ret = -EBUSY;
	if (iface->endpoints[endpoint->endpoint_index] != NULL) {
		dprintk(IOCTL, "endpoint already open\n");
		goto out_with_endpoints_locked;
	}

	rcu_assign_pointer(iface->endpoints[endpoint->endpoint_index], endpoint);
	iface->endpoint_nr++;
	endpoint->iface = iface;

	/* mark the endpoint as open here so that anybody removing this
	 * iface never sees any endpoint in status INIT in the iface list
	 * (only OK and CLOSING are allowed there)
	 */
	endpoint->status = OMX_ENDPOINT_STATUS_OK;

	mutex_unlock(&iface->endpoints_mutex);
	return 0;

 out_with_endpoints_locked:
	mutex_unlock(&iface->endpoints_mutex);
	omx_iface_release(iface);
	goto out;

 out_with_rcu_lock:
	rcu_read_unlock();
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
omx_iface_detach_endpoint(struct omx_endpoint * endpoint,
			  int ifacelocked)
{
	struct omx_iface * iface = endpoint->iface;

	BUG_ON(endpoint->status != OMX_ENDPOINT_STATUS_CLOSING);

	/* lock the list of endpoints in the iface, if needed */
	if (!ifacelocked)
		mutex_lock(&iface->endpoints_mutex);

	BUG_ON(iface->endpoints[endpoint->endpoint_index] != endpoint);
	rcu_assign_pointer(iface->endpoints[endpoint->endpoint_index], NULL);
	/* no need to bother using call_rcu() here, waiting a bit long in synchronize_rcu() is ok */
	synchronize_rcu();

	/* decrease the number of endpoints */
	iface->endpoint_nr--;

	if (!ifacelocked)
		mutex_unlock(&iface->endpoints_mutex);
}

/*
 * Return some info about an endpoint.
 */
int
omx_endpoint_get_info(uint32_t board_index, uint32_t endpoint_index,
		      struct omx_endpoint_info *info)
{
	struct omx_iface * iface;
	struct omx_endpoint * endpoint;
	int ret;

	ret = -EINVAL;
	if (board_index >= omx_iface_max)
		goto out;

	rcu_read_lock();
	iface = rcu_dereference(omx_ifaces[board_index]);
	if (!iface)
		goto out_with_rcu_lock;

	/* keep the rcu lock */

	if (endpoint_index == OMX_RAW_ENDPOINT_INDEX) {
		/* raw endpoint */
		struct omx_iface_raw *raw = &iface->raw;
		if (raw->opener_file) {
			info->closed = 0;
			info->pid = raw->opener_pid;
			strncpy(info->command, raw->opener_comm, OMX_COMMAND_LEN_MAX);
			info->command[OMX_COMMAND_LEN_MAX-1] = '\0';
		} else {
			info->closed = 1;
		}

	} else {
		/* regular endpoint */
	        if (endpoint_index >= omx_endpoint_max)
			goto out_with_rcu_lock;

		endpoint = rcu_dereference(iface->endpoints[endpoint_index]);
		if (endpoint) {
			info->closed = 0;
			info->pid = endpoint->opener_pid;
			strncpy(info->command, endpoint->opener_comm, OMX_COMMAND_LEN_MAX);
			info->command[OMX_COMMAND_LEN_MAX-1] = '\0';
		} else {
			info->closed = 1;
		}
	}

	rcu_read_unlock();
	return 0;

 out_with_rcu_lock:
	rcu_read_unlock();
 out:
	return ret;
}

int
omx_iface_get_endpoint_irq(uint32_t board_index, uint32_t endpoint_index,
			   uint32_t *irq)
{
	struct omx_iface *iface;
	int (*get_endpoint_irq)(struct net_device *, uint32_t, unsigned int *);
	int ret;

	ret = -EINVAL;
	if (board_index >= omx_iface_max)
		goto out;

	rcu_read_lock();
	iface = rcu_dereference(omx_ifaces[board_index]);
	if (!iface)
		goto out_with_rcu_lock;

	ret = -ENODEV;
	if (!iface->get_endpoint_irq_symbol_name)
		goto out_with_rcu_lock;

	get_endpoint_irq = __symbol_get(iface->get_endpoint_irq_symbol_name);
	if (!get_endpoint_irq)
		goto out_with_rcu_lock;

	ret = get_endpoint_irq(iface->eth_ifp, endpoint_index, irq);

	symbol_put_addr(get_endpoint_irq);

 out_with_rcu_lock:
	rcu_read_unlock();
 out:
	return ret;
}

/******************************
 * Netdevice notifier
 */

/*
 * There are no restrictions on this callback since this is a raw notifier chain,
 * it can block, allocate, ...
 */
static int
omx_netdevice_notifier_cb(struct notifier_block *unused,
			   unsigned long event, void *ptr)
{
	struct net_device *ifp = (struct net_device *) ptr;

	if (event == NETDEV_UNREGISTER) {
		struct omx_iface * iface;

		mutex_lock(&omx_ifaces_mutex);
		iface = omx_iface_find_by_ifp(ifp);
		if (iface) {
			int ret;
			printk(KERN_INFO "Open-MX: interface '%s' being unregistered, forcing closing of endpoints...\n",
			       ifp->name);
			/* there is no need to disable incoming packets since
			 * the ethernet ifp is already disabled before the notifier is called
			 */
			ret = omx_iface_detach(iface, 1 /* force */);
			BUG_ON(ret);

			/*
			 * the device will be released when the last reference is actually released,
			 * there's no need to wait for it, the caller will do it in rtnl_unlock()
			 */
		}
		mutex_unlock(&omx_ifaces_mutex);
	}
	/* could check NETDEV_DOWN, NETDEV_UP or NETDEV_CHANGEMTU and report a message */
	/* could check NETDEV_CHANGENAME and update the peer name if unchanged */

	return NOTIFY_DONE;
}

static struct notifier_block omx_netdevice_notifier = {
	.notifier_call = omx_netdevice_notifier_cb,
};

/************************
 * Memory Copy Benchmark
 */

#define OMX_COPYBENCH_BUFLEN (4*1024*1024UL)
#define OMX_COPYBENCH_ITERS 1024

static int
omx_net_copy_bench(void)
{
	void * srcbuf, * dstbuf;
	struct timeval tv1, tv2;
	unsigned long usecs;
	unsigned long nsecs_per_iter;
	unsigned long MB_per_sec;
	int i, err;

	err = -ENOMEM;
	srcbuf = vmalloc(OMX_COPYBENCH_BUFLEN);
	if (!srcbuf)
		goto out;
	dstbuf = vmalloc(OMX_COPYBENCH_BUFLEN);
	if (!dstbuf)
		goto out_with_srcbuf;

	printk("Open-MX: running copy benchmark...\n");

	do_gettimeofday(&tv1);
	for(i=0; i<OMX_COPYBENCH_ITERS; i++)
		memcpy(dstbuf, srcbuf, OMX_COPYBENCH_BUFLEN);
	do_gettimeofday(&tv2);

	usecs = (tv2.tv_sec - tv1.tv_sec)*1000000UL
		+ (tv2.tv_usec - tv1.tv_usec);
	nsecs_per_iter = (usecs * 1000ULL) /OMX_COPYBENCH_ITERS;
	MB_per_sec = OMX_COPYBENCH_BUFLEN / (nsecs_per_iter / 1000);
	printk("Open-MX: memcpy of %ld bytes %d times took %ld us\n",
	       OMX_COPYBENCH_BUFLEN, OMX_COPYBENCH_ITERS, usecs);
	printk("Open-MX: memcpy of %ld bytes took %ld ns (%ld MB/s)\n",
	       OMX_COPYBENCH_BUFLEN, nsecs_per_iter, MB_per_sec);

	err = 0;

	vfree(dstbuf);
 out_with_srcbuf:
	vfree(srcbuf);
 out:
	return err;
}

/******************************
 * Initialization and termination
 */

int
omx_net_init(void)
{
	int ret = 0;

	mutex_init(&omx_ifaces_mutex);

	if (omx_copybench)
		omx_net_copy_bench();

	omx_shared_fake_iface = kzalloc(sizeof(struct omx_iface), GFP_KERNEL);
	if (!omx_shared_fake_iface) {
                printk(KERN_ERR "Open-MX: Failed to the fake iface for shared communication counters\n");
                ret = -ENOMEM;
                goto out;
        }

	omx_ifaces = kzalloc(omx_iface_max * sizeof(struct omx_iface *), GFP_KERNEL);
	if (!omx_ifaces) {
		printk(KERN_ERR "Open-MX: failed to allocate interface array\n");
		ret = -ENOMEM;
		goto out_with_shared_fake_iface;
	}

	ret = register_netdevice_notifier(&omx_netdevice_notifier);
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: failed to register netdevice notifier\n");
		goto out_with_ifaces;
	}

	omx_pkt_types_init();
	dev_add_pack(&omx_pt);

	if (omx_delayed_ifnames) {
		/* attach ifaces whose name are in ifnames (limited to omx_iface_max) */
		/* module parameter values are guaranteed to be \0-terminated */
		omx_ifaces_store(omx_delayed_ifnames);
		kfree(omx_delayed_ifnames);

	} else {
		/* attach everything ethernet/up/large-mtu (limited to omx_iface_max) */
		struct net_device * ifp;

		read_lock(&dev_base_lock);
		omx_for_each_netdev(ifp) {
			/* check that it is an Ethernet device, that it is up, and that the MTU is large enough */
			if (ifp->type != ARPHRD_ETHER) {
				printk(KERN_INFO "Open-MX: not attaching non-Ethernet interface '%s' by default\n",
				       ifp->name);
				continue;
			} else if (!(dev_get_flags(ifp) & IFF_UP)) {
				printk(KERN_INFO "Open-MX: not attaching non-up interface '%s' by default\n",
				       ifp->name);
				continue;
			} else if (ifp->mtu < OMX_MTU) {
				printk(KERN_INFO "Open-MX: not attaching interface '%s' with small MTU %d by default\n",
				       ifp->name, ifp->mtu);
				continue;
			}

			dev_hold(ifp);
			if (omx_iface_attach(ifp) < 0) {
				dev_put(ifp);
				break;
			}
		}
		read_unlock(&dev_base_lock);
	}

	printk(KERN_INFO "Open-MX: attached %d interfaces\n", omx_iface_nr);
	return 0;

 out_with_ifaces:
	kfree(omx_ifaces);
 out_with_shared_fake_iface:
	kfree(omx_shared_fake_iface);
 out:
	return ret;
}

void
omx_net_exit(void)
{
	int i, nr = 0;

	/* module unloading cannot happen before all users exit
	 * since they hold a reference on the chardev.
	 * so all endpoints are closed once we arrive here.
	 */

	dev_remove_pack(&omx_pt);
	/*
	 * Now, no iface may be used by any incoming packet
	 * and there is no packet being processed either.
	 *
	 * All iface references are from user-space through endpoints.
	 */

	/* prevent omx_netdevice_notifier from removing an iface now */
	mutex_lock(&omx_ifaces_mutex);

	for (i=0; i<omx_iface_max; i++) {
		struct omx_iface * iface = omx_ifaces[i];
		if (iface != NULL) {
			int ret;

			/* detach the iface now.
			 * all endpoints are closed since there is no reference on the module,
			 * no need to force
			 */
			ret = omx_iface_detach(iface, 0);
			BUG_ON(ret);
			nr++;
		}
	}
	printk(KERN_INFO "Open-MX: detached %d interfaces\n", nr);

	/* release the lock to let omx_netdevice_notifier finish
	 * in case it has been invoked during our loop
	 */
	mutex_unlock(&omx_ifaces_mutex);

	/* unregister the notifier then */
	unregister_netdevice_notifier(&omx_netdevice_notifier);

	/* free structures now that the notifier is gone */
	kfree(omx_ifaces);
	kfree(omx_shared_fake_iface);

	/* FIXME: some pull handle timers may still be active */
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
