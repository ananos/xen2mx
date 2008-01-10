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
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#include "omx_common.h"
#include "omx_peer.h"
#include "omx_misc.h"
#include "omx_hal.h"
#include "omx_wire_access.h"

/* defined as a module parameter */
extern int omx_peer_max;

static rwlock_t omx_peer_lock;
static struct omx_peer ** omx_peer_array;
static struct list_head * omx_peer_addr_hash_array;
static int omx_peer_next_nr;

#define OMX_PEER_ADDR_HASH_NR 256

static inline uint8_t
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

	write_lock_bh(&omx_peer_lock);
	for(i=0; i<omx_peer_max; i++) {
		struct omx_peer * peer = omx_peer_array[i];
		if (!peer)
			continue;
		kfree(peer->hostname);
		list_del(&peer->addr_hash_elt);
		kfree(peer);
		omx_peer_array[i] = NULL;
	}
	omx_peer_next_nr = 0;
	write_unlock_bh(&omx_peer_lock);
}

static int
omx_peer_is_local(uint64_t board_addr)
{
	struct net_device * ifp;
	int ret = 0;

	read_lock(&dev_base_lock);
	omx_for_each_netdev(ifp) {
		if (board_addr == omx_board_addr_from_netdevice(ifp)) {
			ret = 1;
			break;
		}
	}
	read_unlock(&dev_base_lock);

	return ret;
}

int
omx_peer_add(uint64_t board_addr, char *hostname)
{
	struct omx_peer * peer, * existing;
	uint8_t hash;
	int err;

	err = -EINVAL;
	if (!hostname)
		goto out;

	err = -ENOMEM;
	peer = kmalloc(sizeof(*peer), GFP_KERNEL);
	if (!peer)
		goto out;

	peer->board_addr = board_addr;
	peer->hostname = kstrdup(hostname, GFP_KERNEL);

	write_lock_bh(&omx_peer_lock);

	err = -ENOMEM;
	if (omx_peer_next_nr == omx_peer_max)
		goto out_with_lock;

	hash = omx_peer_addr_hash(board_addr);

	list_for_each_entry(existing, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (existing->board_addr == board_addr) {
			printk(KERN_INFO "Open-MX: Cannot add already existing peer address %012llx\n",
			       (unsigned long long) board_addr);
			err = -EBUSY;
			goto out_with_lock;
		}
	}

	peer->index = omx_peer_next_nr;

	if (omx_peer_is_local(board_addr)) {
		dprintk(PEER, "adding peer %d with addr %012llx (local peer)\n",
			peer->index, (unsigned long long) board_addr);
		peer->reverse_index = peer->index;
	} else {
		dprintk(PEER, "adding peer %d with addr %012llx\n",
			peer->index, (unsigned long long) board_addr);
		peer->reverse_index = OMX_UNKNOWN_REVERSE_PEER_INDEX;
	}

	list_add_tail(&peer->addr_hash_elt, &omx_peer_addr_hash_array[hash]);
	omx_peer_array[omx_peer_next_nr] = peer;
	omx_peer_next_nr++;
	write_unlock_bh(&omx_peer_lock);

	return 0;

 out_with_lock:
	write_unlock_bh(&omx_peer_lock);
	kfree(peer->hostname);
	kfree(peer);	
 out:
	return err;
}

int
omx_peer_set_reverse_index(uint16_t index, uint16_t reverse_index)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	read_lock_bh(&omx_peer_lock);

	peer = omx_peer_array[index];
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

	read_unlock_bh(&omx_peer_lock);
	return 0;

 out_with_lock:
	read_unlock_bh(&omx_peer_lock);
 out:
	return err;
}

int
omx_set_target_peer(struct omx_hdr *mh, uint16_t index)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	read_lock_bh(&omx_peer_lock);

	peer = omx_peer_array[index];
	if (!peer)
		goto out_with_lock;

	omx_board_addr_to_ethhdr_dst(&mh->head.eth, peer->board_addr);
	OMX_PKT_FIELD_FROM(mh->head.dst_src_peer_index, peer->reverse_index);

	read_unlock_bh(&omx_peer_lock);
	return 0;

 out_with_lock:
	read_unlock_bh(&omx_peer_lock);
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

	read_lock_bh(&omx_peer_lock);

	peer = omx_peer_array[index];
	if (!peer)
		goto out_with_lock;

	if (board_addr)
		*board_addr = peer->board_addr;
	if (hostname)
		strcpy(hostname, peer->hostname);

	read_unlock_bh(&omx_peer_lock);
	return 0;

 out_with_lock:
	read_unlock_bh(&omx_peer_lock);
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

	list_for_each_entry(peer, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (peer->board_addr == board_addr) {
			if (index)
				*index = peer->index;
			if (hostname)
				strcpy(hostname, peer->hostname);
			return 0;
		}
	}

	return -EINVAL;

}

int
omx_peer_lookup_by_hostname(char *hostname,
			    uint64_t *board_addr, uint32_t *index)
{
	int i;

	read_lock_bh(&omx_peer_lock);

	for(i=0; i<omx_peer_max; i++) {
		struct omx_peer *peer = omx_peer_array[i];
		if (!strcmp(hostname, peer->hostname)) {
			if (index)
				*index = i;
			if (board_addr)
				*board_addr = peer->board_addr;
			read_unlock_bh(&omx_peer_lock);
			return 0;
		}
	}

	read_unlock_bh(&omx_peer_lock);
	return -EINVAL;
}

int
omx_peers_init(void)
{
	int err;
	int i;

	rwlock_init(&omx_peer_lock);

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
