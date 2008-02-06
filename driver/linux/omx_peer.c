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
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#ifdef OMX_HAVE_MUTEX
#include <linux/mutex.h>
#endif
#include <linux/rcupdate.h>

#include "omx_common.h"
#include "omx_peer.h"
#include "omx_iface.h"
#include "omx_endpoint.h"
#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"

/* defined as a module parameter */
extern int omx_peer_max;

static struct omx_peer ** omx_peer_array;
static struct list_head * omx_peer_addr_hash_array;
struct mutex omx_peers_mutex; /* big mutex protecting concurrent modification, readers are protected by RCU */
static int omx_peer_next_nr;

#define OMX_PEER_ADDR_HASH_NR 256

static INLINE uint8_t
omx_peer_addr_hash(uint64_t board_addr)
{
	uint32_t tmp24;
	uint8_t tmp8;

	tmp24 = board_addr ^ (board_addr >> 24);
	tmp8 = tmp24 ^ (tmp24 >> 8) ^ (tmp24 >> 16);
	return tmp8;
}

void
omx_peers_clear(void)
{
	int i;

	dprintk(PEER, "clearing all peers\n");

	mutex_lock(&omx_peers_mutex);

	for(i=0; i<omx_peer_max; i++) {
		struct omx_peer * peer = omx_peer_array[i];
		struct omx_iface * iface;

		if (!peer)
			continue;

		list_del_rcu(&peer->addr_hash_elt);
		rcu_assign_pointer(omx_peer_array[i], NULL);
		/* no need to bother using call_rcu() here, waiting a bit long in synchronize_rcu() is ok */
		synchronize_rcu();

		iface = peer->local_iface;
		if (iface) {
			dprintk(PEER, "detaching iface %s (%s) peer #%d\n",
				iface->eth_ifp->name, peer->hostname, peer->index);

			peer->index = OMX_UNKNOWN_REVERSE_PEER_INDEX;

			/* release the iface reference now it is not linked in the peer table anymore */
			omx_iface_release(iface);
		} else {
			kfree(peer->hostname);
			kfree(peer);
		}
	}
	omx_peer_next_nr = 0;

	mutex_unlock(&omx_peers_mutex);
}

int
omx_peer_add(uint64_t board_addr, char *hostname)
{
	struct omx_peer * new, * old;
	struct omx_iface * iface;
	char * new_hostname;
	uint8_t hash;
	int err;

	err = -EINVAL;
	if (!hostname)
		goto out;

	err = -ENOMEM;
	new_hostname = kstrdup(hostname, GFP_KERNEL);
	if (!new_hostname)
		goto out;

	mutex_lock(&omx_peers_mutex);

	err = -ENOMEM;
	if (omx_peer_next_nr == omx_peer_max)
		goto out_with_mutex;

	hash = omx_peer_addr_hash(board_addr);

	rcu_read_lock();
	list_for_each_entry_rcu(old, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (old->board_addr == board_addr) {
			printk(KERN_INFO "Open-MX: Cannot add already existing peer address %012llx\n",
			       (unsigned long long) board_addr);
			err = -EBUSY;
			rcu_read_unlock();
			goto out_with_mutex;
		}
	}
	rcu_read_unlock();

	iface = omx_iface_find_by_addr(board_addr);
	if (iface) {
		char * old_hostname;

		/* omx_iface_find_by_addr() acquired the iface, keep the reference */
		new = &iface->peer;
		new->local_iface = iface;

		/* replace the iface hostname withe one from the peer table */
		old_hostname = new->hostname;
		new->hostname = new_hostname;
		dprintk(PEER, "using iface %s (%s) to add new local peer %s address %012llx\n",
			iface->eth_ifp->name, old_hostname,
			new_hostname, (unsigned long long) board_addr);
		printk(KERN_INFO "Open-MX: Renaming iface %s (%s) into peer name %s\n",
		       iface->eth_ifp->name, old_hostname, new_hostname);
		kfree(old_hostname);

	} else {
		err = -ENOMEM;
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (!new)
			goto out_with_mutex;

		new->board_addr = board_addr;
		new->hostname = new_hostname;
	}

	new->index = omx_peer_next_nr;
	new->local_iface = iface;

	if (iface) {
		dprintk(PEER, "adding peer %d with addr %012llx (local peer)\n",
			new->index, (unsigned long long) board_addr);
		new->reverse_index = new->index;
	} else {
		dprintk(PEER, "adding peer %d with addr %012llx\n",
			new->index, (unsigned long long) board_addr);
		new->reverse_index = OMX_UNKNOWN_REVERSE_PEER_INDEX;
	}

	list_add_tail_rcu(&new->addr_hash_elt, &omx_peer_addr_hash_array[hash]);
	rcu_assign_pointer(omx_peer_array[omx_peer_next_nr], new);
	omx_peer_next_nr++;

	mutex_unlock(&omx_peers_mutex);

	return 0;

 out_with_mutex:
	mutex_unlock(&omx_peers_mutex);
	kfree(new_hostname);
 out:
	return err;
}

void
omx_peers_notify_iface_attach(struct omx_iface * iface)
{
	struct omx_peer * new, * old;
	uint64_t board_addr;
	uint8_t hash;

	new = &iface->peer;
	board_addr = new->board_addr;
	hash = omx_peer_addr_hash(board_addr);

	mutex_lock(&omx_peers_mutex);

	list_for_each_entry(old, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (old->board_addr == board_addr) {
			uint32_t index = old->index;

			dprintk(PEER, "attaching local iface %s (%s) with address %012llx as peer #%d %s\n",
				iface->eth_ifp->name, new->hostname, (unsigned long long) board_addr,
				index, old->hostname);
			printk(KERN_INFO "Open-MX: Renaming new iface %s (%s) into peer name %s\n",
			       iface->eth_ifp->name, new->hostname, old->hostname);

			/* take a reference on the iface */
			kref_get(&iface->refcount);

			/* board_addr already set */
			new->index = index;
			new->reverse_index = index;

			/* replace the iface hostname with the one from the peer table */
			kfree(new->hostname);
			new->hostname = old->hostname;
			new->local_iface = iface;

			rcu_assign_pointer(omx_peer_array[index], new);
			list_replace_rcu(&old->addr_hash_elt, &new->addr_hash_elt);
			/* no need to bother using call_rcu() here, waiting a bit long in synchronize_rcu() is ok */
			synchronize_rcu();
			kfree(old);
			break;
		}
	}

	mutex_unlock(&omx_peers_mutex);
}

void
omx_peers_notify_iface_detach(struct omx_iface * iface)
{
	struct omx_peer * peer;

	peer = &iface->peer;

	mutex_lock(&omx_peers_mutex);

	if (peer->index != OMX_UNKNOWN_REVERSE_PEER_INDEX) {
		uint32_t index = peer->index;

		dprintk(PEER, "detaching iface %s (%s) peer #%d\n",
			iface->eth_ifp->name, peer->hostname, index);

		/* the iface is in the array, just remove it, we don't really care about still having it in the peer table */
		list_del_rcu(&peer->addr_hash_elt);
		rcu_assign_pointer(omx_peer_array[index], NULL);
		/* no need to bother using call_rcu() here, waiting a bit long in synchronize_rcu() is ok */
		synchronize_rcu();
		peer->index = OMX_UNKNOWN_REVERSE_PEER_INDEX;

		/* release the iface reference now it is not linked in the peer table anymore */
		omx_iface_release(iface);
	}

	mutex_unlock(&omx_peers_mutex);
}

int
omx_peer_set_reverse_index(uint16_t index, uint16_t reverse_index)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	rcu_read_lock();

	peer = rcu_dereference(omx_peer_array[index]);
	if (!peer)
		goto out_with_lock;

	if (peer->reverse_index != OMX_UNKNOWN_REVERSE_PEER_INDEX
	    && reverse_index != peer->reverse_index)
		dprintk(PEER, "changing remote peer #%d reverse index from %d to %d\n",
			index, peer->reverse_index, reverse_index);
	else
		dprintk(PEER, "setting remote peer #%d reverse index to %d\n",
			index, reverse_index);

	peer->reverse_index = reverse_index;

	rcu_read_unlock();
	return 0;

 out_with_lock:
	rcu_read_unlock();
 out:
	return err;
}

/*
 * returns an acquired endpoint, or NULL if the peer is not local,
 * ot PTR_ERR(errno) if the peer is local but the endpoint invalid
 */
struct omx_endpoint *
omx_local_peer_acquire_endpoint(uint16_t peer_index, uint8_t endpoint_index)
{
	struct omx_peer *peer;
	struct omx_iface *iface;
	struct omx_endpoint *endpoint;

	if (peer_index >= omx_peer_max)
		goto out;

	rcu_read_lock();

	peer = rcu_dereference(omx_peer_array[peer_index]);
	if (!peer)
		goto out_with_lock;

	iface = peer->local_iface;
	if (!iface)
		goto out_with_lock;

	endpoint = omx_endpoint_acquire_by_iface_index(iface, endpoint_index);

	rcu_read_unlock();
	return endpoint;

 out_with_lock:
	rcu_read_unlock();
 out:
	return NULL;
}

int
omx_set_target_peer(struct omx_hdr *mh, uint16_t index)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	rcu_read_lock();

	peer = rcu_dereference(omx_peer_array[index]);
	if (!peer)
		goto out_with_lock;

	omx_board_addr_to_ethhdr_dst(&mh->head.eth, peer->board_addr);
	OMX_PKT_FIELD_FROM(mh->head.dst_src_peer_index, peer->reverse_index);

	rcu_read_unlock();
	return 0;

 out_with_lock:
	rcu_read_unlock();
 out:
	return err;
}

int
omx_check_recv_peer_index(uint16_t index)
{
	/* the table is never reduced, no need to lock */
	if (index >= omx_peer_max
	    || !omx_peer_array[index])
		return -EINVAL;

	return 0;
}

int
omx_peer_lookup_by_index(uint32_t index,
			 uint64_t *board_addr, char *hostname)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	rcu_read_lock();

	peer = rcu_dereference(omx_peer_array[index]);
	if (!peer)
		goto out_with_lock;

	if (board_addr)
		*board_addr = peer->board_addr;
	if (hostname)
		strcpy(hostname, peer->hostname);

	rcu_read_unlock();
	return 0;

 out_with_lock:
	rcu_read_unlock();
 out:
	return err;
}

int
omx_peer_lookup_by_addr(uint64_t board_addr,
			char *hostname, uint32_t *index)
{
	uint8_t hash;
	struct omx_peer * peer;

	hash = omx_peer_addr_hash(board_addr);

	rcu_read_lock();
	list_for_each_entry_rcu(peer, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (peer->board_addr == board_addr) {
			if (index)
				*index = peer->index;
			if (hostname)
				strcpy(hostname, peer->hostname);
			return 0;
		}
	}
	rcu_read_unlock();

	return -EINVAL;

}

int
omx_peer_lookup_by_hostname(char *hostname,
			    uint64_t *board_addr, uint32_t *index)
{
	int i;

	rcu_read_lock();
	for(i=0; i<omx_peer_max; i++) {
		struct omx_peer *peer = rcu_dereference(omx_peer_array[i]);
		if (!peer)
			continue;

		if (!strcmp(hostname, peer->hostname)) {
			if (index)
				*index = i;
			if (board_addr)
				*board_addr = peer->board_addr;
			rcu_read_unlock();
			return 0;
		}
	}
	rcu_read_unlock();

	return -EINVAL;
}

int
omx_peers_init(void)
{
	int err;
	int i;

	mutex_init(&omx_peers_mutex);

	omx_peer_next_nr = 0;

	omx_peer_array = vmalloc(omx_peer_max * sizeof(*omx_peer_array));
	if (!omx_peer_array) {
		printk(KERN_ERR "Open-MX: Failed to allocate the peer array\n");
		err = -ENOMEM;
		goto out;
	}
	for(i=0; i<omx_peer_max; i++)
		omx_peer_array[i] = NULL;

	omx_peer_addr_hash_array = kmalloc(OMX_PEER_ADDR_HASH_NR * sizeof(*omx_peer_addr_hash_array), GFP_KERNEL);
	if (!omx_peer_addr_hash_array) {
		printk(KERN_ERR "Open-MX: Failed to allocate the peer addr hash array\n");
		err = -ENOMEM;
		goto out_with_peer_array;
	}
	for(i=0; i<OMX_PEER_ADDR_HASH_NR; i++)
		INIT_LIST_HEAD(&omx_peer_addr_hash_array[i]);

	return 0;

 out_with_peer_array:
	vfree(omx_peer_array);
 out:
	return err;
}

void
omx_peers_exit(void)
{
	omx_peers_clear();
	kfree(omx_peer_addr_hash_array);
	vfree(omx_peer_array);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
