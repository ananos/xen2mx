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

#include "omx_common.h"

struct omx_peer {
	uint64_t board_addr;
	char *hostname;
	uint32_t index; /* this peer index in our table */
	uint32_t reverse_index; /* our index in this peer table, -1 if unknown */
};

static spinlock_t omx_peer_lock;
static struct omx_peer ** omx_peer_array;
static int omx_peers_nr;

void
omx_peers_clear(void)
{
	int i;

	spin_lock(&omx_peer_lock);
	for(i=0; i<omx_peers_nr; i++) {
		struct omx_peer * peer = omx_peer_array[i];
		if (peer) {
			kfree(peer->hostname);
			kfree(peer);
			omx_peer_array[i] = NULL;
		}
	}
	omx_peers_nr = 0;
	spin_unlock(&omx_peer_lock);
}

int
omx_peer_add(uint64_t board_addr, char *hostname)
{
	struct omx_peer * peer;

	if (omx_peers_nr == omx_peer_max)
		return -ENOMEM;

	peer = kmalloc(sizeof(*peer), GFP_KERNEL);
	if (!peer)
		return -ENOMEM;

	if (!hostname)
		return -EINVAL;

	peer->board_addr = board_addr;
	peer->hostname = kstrdup(hostname, GFP_KERNEL);
	peer->reverse_index = -1; /* unknown for now */

	spin_lock(&omx_peer_lock);
	omx_peer_array[omx_peers_nr] = peer;
	peer->index = omx_peers_nr;
	omx_peers_nr++;
	spin_unlock(&omx_peer_lock);

	return 0;
}

int
omx_peer_lookup(unsigned cmd,
		uint64_t *board_addr, char *hostname, uint32_t *index)
{
	struct omx_peer *peer;
	int err = 0;

	spin_lock(&omx_peer_lock);

	switch (cmd) {

	case OMX_CMD_PEER_FROM_INDEX: {
		peer = omx_peer_array[*index];
		if (!peer) {
			err = -EINVAL;
			goto out;
		}

		*board_addr = peer->board_addr;
		strcpy(hostname, peer->hostname);

		break;
	}

	case OMX_CMD_PEER_FROM_ADDR: {
		int i;

		for(i=0; i<omx_peers_nr; i++) {
			peer = omx_peer_array[i];
			if (peer->board_addr == *board_addr) {
				*index = i;
				strcpy(hostname, peer->hostname);
				goto out;
			}
		}

		err = -EINVAL;
		break;
	}

	case OMX_CMD_PEER_FROM_HOSTNAME: {
		int i;

		for(i=0; i<omx_peers_nr; i++) {
			peer = omx_peer_array[i];
			if (!strcmp(hostname, peer->hostname)) {
				*index = i;
				*board_addr = peer->board_addr;
				goto out;
			}
		}

		err = -EINVAL;
		break;
	}

	default:
		BUG();
	}

 out:
	spin_unlock(&omx_peer_lock);

	return err;
}

int
omx_peers_init(void)
{
	int err;
	int i;

	spin_lock_init(&omx_peer_lock);

	omx_peers_nr = 0;

	omx_peer_array = kmalloc(omx_peer_max * sizeof(*omx_peer_array), GFP_KERNEL);
	if (!omx_peer_array) {
		printk(KERN_ERR "Open-MX: Failed to allocate the peer array\n");
		err = -ENOMEM;
		goto out;
	}
	for(i=0; i<omx_peer_max; i++)
		omx_peer_array[i] = NULL;

	return 0;

 out:
	return err;
}

void
omx_peers_exit(void)
{
	omx_peers_clear();
	kfree(omx_peer_array);
	omx_peer_array = NULL;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
