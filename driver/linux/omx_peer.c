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
#include <linux/skbuff.h>
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

static struct omx_peer ** omx_peer_array;
static struct list_head * omx_peer_addr_hash_array;
static int omx_peer_next_nr;
static struct list_head omx_host_query_peer_list;

 /*
  * Big mutex protecting concurrent modifications of the peer table:
  *  - per-index array of peers
  *  - hashed lists
  *  - next_nr
  *  - all peer hostnames (never accessed by the bottom half)
  *  - the host_query peer list
  *
  * The bottom only access the peer (not its hostname) to get peer indexes,
  * so we use rcu there.
  */
static struct mutex omx_peers_mutex;

/* magic number used in host_query/reply */
static int omx_host_query_magic = 0x13052008;

#define OMX_PEER_ADDR_HASH_NR 256

/************************
 * Peer Table Management
 */

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

			BUG_ON(!peer->hostname);
			/* local iface peer hostname cannot be NULL, no need to update the host_query_list or so */

			peer->index = OMX_UNKNOWN_REVERSE_PEER_INDEX;

			/* release the iface reference now it is not linked in the peer table anymore */
			omx_iface_release(iface);
		} else {
			if (!peer->hostname) {
				list_del(&peer->host_query_list_elt);
				dprintk(PEER, "peer does not need host query anymore\n");
			}

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
	struct omx_peer * peer;
	struct omx_iface * iface;
	char * new_hostname = NULL;
	uint16_t index;
	uint8_t hash;
	int already_hashed = 0;
	int err;

	if (hostname) {
		err = -ENOMEM;
		new_hostname = kstrdup(hostname, GFP_KERNEL);
		if (!new_hostname)
			goto out;
	}

	mutex_lock(&omx_peers_mutex);

	/* does the peer exist ? */
	hash = omx_peer_addr_hash(board_addr);
	list_for_each_entry(peer, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (peer->board_addr == board_addr) {
			already_hashed = 1;
			break;
		}
	}

	/* if not already hashed, check that we can get a new peer index */
	if (!already_hashed) {
		err = -ENOMEM;
		if (omx_peer_next_nr == omx_peer_max)
			goto out_with_mutex;
		peer = NULL;
	}

	iface = omx_iface_find_by_addr(board_addr);
	if (iface) {
		/* add local iface to peer table and update the iface name if possible */

		/* omx_iface_find_by_addr() acquired the iface, keep the reference */
		peer = &iface->peer;
		peer->local_iface = iface;

		/* replace the iface hostname with the one from the peer table if non-null */
		if (new_hostname) {
			char * old_hostname = peer->hostname;
			peer->hostname = new_hostname;

			dprintk(PEER, "using iface %s (%s) to add new local peer %s address %012llx\n",
				iface->eth_ifp->name, old_hostname,
				new_hostname, (unsigned long long) board_addr);
			printk(KERN_INFO "Open-MX: Renaming iface %s (%s) into peer name %s\n",
			       iface->eth_ifp->name, old_hostname, new_hostname);

			BUG_ON(!old_hostname);
			/* local iface peer hostname cannot be NULL, no need to update the host_query_list or so */
			kfree(old_hostname);
		}

	} else if (already_hashed) {
		/* just update the hostname of the existing peer */
		char * old_hostname = peer->hostname;

		dprintk(PEER, "renaming peer %s into peer name %s\n",
			old_hostname, new_hostname);

		if (!old_hostname && new_hostname) {
			list_del(&peer->host_query_list_elt);
			dprintk(PEER, "peer does not need host query anymore\n");
		} else if (old_hostname && !new_hostname) {
			list_add_tail(&peer->host_query_list_elt, &omx_host_query_peer_list);
			peer->host_query_last_resend_jiffies = 0;
			dprintk(PEER, "peer needs host query\n");
		}

		peer->hostname = new_hostname;
		kfree(old_hostname);

	} else {
		/* actually add a new peer */

		err = -ENOMEM;
		peer = kmalloc(sizeof(*peer), GFP_KERNEL);
		if (!peer)
			goto out_with_mutex;

		peer->board_addr = board_addr;
		peer->hostname = new_hostname;
		peer->local_iface = NULL;

		if (!new_hostname) {
			list_add_tail(&peer->host_query_list_elt, &omx_host_query_peer_list);
			peer->host_query_last_resend_jiffies = 0;
			dprintk(PEER, "peer needs host query\n");
	       	}
	}

	if (!already_hashed) {
		/* this is a new peer, allocate an index and hash it */
		peer->index = index = omx_peer_next_nr;

		if (iface) {
			dprintk(PEER, "adding peer %d with addr %012llx (local peer)\n",
				peer->index, (unsigned long long) board_addr);
			peer->reverse_index = peer->index;
		} else {
			dprintk(PEER, "adding peer %d with addr %012llx\n",
				peer->index, (unsigned long long) board_addr);
			peer->reverse_index = OMX_UNKNOWN_REVERSE_PEER_INDEX;
		}

		list_add_tail_rcu(&peer->addr_hash_elt, &omx_peer_addr_hash_array[hash]);
		rcu_assign_pointer(omx_peer_array[omx_peer_next_nr], peer);
		omx_peer_next_nr++;
	}

	mutex_unlock(&omx_peers_mutex);

	return 0;

 out_with_mutex:
	mutex_unlock(&omx_peers_mutex);
	kfree(new_hostname);
 out:
	return err;
}

/*************************
 * Local Iface Management
 */

void
omx_peers_notify_iface_attach(struct omx_iface * iface)
{
	struct omx_peer * peer, * ifacepeer;
	uint64_t board_addr;
	uint8_t hash;

	ifacepeer = &iface->peer;
	board_addr = ifacepeer->board_addr;
	hash = omx_peer_addr_hash(board_addr);

	mutex_lock(&omx_peers_mutex);

	list_for_each_entry(peer, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (peer->board_addr == board_addr) {
			uint32_t index = peer->index;

			dprintk(PEER, "attaching local iface %s (%s) with address %012llx as peer #%d %s\n",
				iface->eth_ifp->name, ifacepeer->hostname, (unsigned long long) board_addr,
				index, peer->hostname);
			printk(KERN_INFO "Open-MX: Renaming new iface %s (%s) into peer name %s\n",
			       iface->eth_ifp->name, ifacepeer->hostname, peer->hostname);

			/* take a reference on the iface */
			kref_get(&iface->refcount);

			/* board_addr already set */
			ifacepeer->index = index;
			ifacepeer->reverse_index = index;
			ifacepeer->local_iface = iface;

			/* replace the iface hostname with the one from the peer table if it exists */
			if (peer->hostname) {
				char * new_hostname = ifacepeer->hostname;
				ifacepeer->hostname = peer->hostname;
				kfree(new_hostname);
			} else {
				list_del(&peer->host_query_list_elt);
				dprintk(PEER, "peer does not need host query anymore\n");
			}

			rcu_assign_pointer(omx_peer_array[index], ifacepeer);
			list_replace_rcu(&peer->addr_hash_elt, &ifacepeer->addr_hash_elt);
			/* no need to bother using call_rcu() here, waiting a bit long in synchronize_rcu() is ok */
			synchronize_rcu();
			kfree(peer);
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

/************************
 * Peer Index Management
 */

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

int
omx_set_target_peer(struct omx_pkt_head *ph, uint16_t index)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	rcu_read_lock();

	peer = rcu_dereference(omx_peer_array[index]);
	if (!peer)
		goto out_with_lock;

	omx_board_addr_to_ethhdr_dst(&ph->eth, peer->board_addr);
	OMX_PKT_FIELD_FROM(ph->dst_src_peer_index, peer->reverse_index);

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

/**************
 * Peer Lookup
 */

int
omx_peer_lookup_by_index(uint32_t index,
			 uint64_t *board_addr, char *hostname)
{
	struct omx_peer *peer;
	int err = -EINVAL;

	if (index >= omx_peer_max)
		goto out;

	mutex_lock(&omx_peers_mutex);

	peer = rcu_dereference(omx_peer_array[index]);
	if (!peer)
		goto out_with_lock;

	if (board_addr)
		*board_addr = peer->board_addr;

	if (hostname) {
		if (peer->hostname)
			strcpy(hostname, peer->hostname);
		else
			hostname[0] = '\0';
	}

	mutex_unlock(&omx_peers_mutex);
	return 0;

 out_with_lock:
	mutex_unlock(&omx_peers_mutex);
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

	mutex_lock(&omx_peers_mutex);

	list_for_each_entry(peer, &omx_peer_addr_hash_array[hash], addr_hash_elt) {
		if (peer->board_addr == board_addr) {

			if (index)
				*index = peer->index;

			if (hostname) {
				if (peer->hostname)
					strcpy(hostname, peer->hostname);
				else
					hostname[0] = '\0';
			}

			mutex_unlock(&omx_peers_mutex);
			return 0;
		}
	}

	mutex_unlock(&omx_peers_mutex);

	return -EINVAL;

}

int
omx_peer_lookup_by_hostname(char *hostname,
			    uint64_t *board_addr, uint32_t *index)
{
	int i;

	mutex_lock(&omx_peers_mutex);

	for(i=0; i<omx_peer_max; i++) {
		struct omx_peer *peer = rcu_dereference(omx_peer_array[i]);
		if (!peer || !peer->hostname)
			continue;

		if (!strcmp(hostname, peer->hostname)) {
			if (index)
				*index = i;
			if (board_addr)
				*board_addr = peer->board_addr;
			mutex_unlock(&omx_peers_mutex);
			return 0;
		}
	}

	mutex_unlock(&omx_peers_mutex);

	return -EINVAL;
}

/******************************
 * Host Query/Reply Management
 */

static void
omx_peer_host_query(struct omx_peer *peer)
{
	uint64_t peer_addr = peer->board_addr;
	uint16_t peer_index = peer->index;
	struct sk_buff *skb;
	struct omx_hdr *mh;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_host_query *query_n;
	int ret = 0;

	dprintk(QUERY, "sending host query for peer %d\n", peer_index);

	skb = omx_new_skb(ETH_ZLEN);
	if (unlikely(skb == NULL)) {
		printk(KERN_INFO "Open-MX: Failed to create host query skb\n");
		ret = -ENOMEM;
 		goto out;
	}

	/* locate headers */
	mh = omx_skb_mac_header(skb);
	ph = &mh->head;
	eh = &ph->eth;
	query_n = (struct omx_pkt_host_query *) (ph + 1);

	/* fill query */
	OMX_PKT_FIELD_FROM(query_n->ptype, OMX_PKT_TYPE_HOST_QUERY);
	OMX_PKT_FIELD_FROM(query_n->return_peer_index, peer_index);
	OMX_PKT_FIELD_FROM(query_n->magic, omx_host_query_magic);

	/* fill ethernet header, except the source for now */
        eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
	omx_board_addr_to_ethhdr_dst(&ph->eth, peer_addr);

	/* send on all attached interfaces */
	omx_send_on_all_ifaces(skb);

 out:
	return;
}

#define OMX_HOST_QUERY_RESEND_JIFFIES HZ

void
omx_process_peers_to_host_query(void)
{
	struct omx_peer *peer;

	mutex_lock(&omx_peers_mutex);

	list_for_each_entry(peer, &omx_host_query_peer_list, host_query_list_elt) {
		dprintk(QUERY, "need to query peer %d?\n", peer->index);
		if (peer->host_query_last_resend_jiffies + OMX_HOST_QUERY_RESEND_JIFFIES > jiffies)
			break;

		omx_peer_host_query(peer);
		peer->host_query_last_resend_jiffies = jiffies;
		list_del(&peer->host_query_list_elt);
		list_add_tail(&peer->host_query_list_elt, &omx_host_query_peer_list);
	}

	mutex_unlock(&omx_peers_mutex);
}

static struct sk_buff_head omx_host_query_list;
static struct sk_buff_head omx_host_reply_list;

int
omx_recv_host_query(struct omx_iface * iface,
		    struct omx_hdr * mh,
		    struct sk_buff * skb)
{
	struct net_device * ifp = iface->eth_ifp;
	struct omx_pkt_head *ph;
	struct ethhdr *eh;
	struct omx_pkt_host_query *query_n;
	uint32_t magic;

	/* locate headers */
	ph = &mh->head;
	eh = &ph->eth;
	query_n = (struct omx_pkt_host_query *) (ph + 1);
	magic = OMX_FROM_PKT_FIELD(query_n->magic);

	if (memcmp(&eh->h_dest, ifp->dev_addr, sizeof (eh->h_dest))) {
		/* not for this iface, ignore */
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	/* store the iface to avoid having to recompute it later */
	skb->sk = (void *) iface;

	skb_queue_tail(&omx_host_query_list, skb);
	dprintk(QUERY, "got host query\n");
	omx_counter_inc(iface, RECV_HOST_QUERY);
	return 0;
}

int
omx_recv_host_reply(struct omx_iface * iface,
		    struct omx_hdr * mh,
		    struct sk_buff * skb)
{
	struct omx_pkt_head *ph;
	struct omx_pkt_host_reply *reply_n;
	uint32_t magic;

	/* locate headers */
	ph = &mh->head;
	reply_n = (struct omx_pkt_host_reply *) (ph + 1);
	magic = OMX_FROM_PKT_FIELD(reply_n->magic);

	if (magic != omx_host_query_magic) {
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	/* store the iface to avoid having to recompute it later */
	skb->sk = (void *) iface;

	skb_queue_tail(&omx_host_reply_list, skb);
	omx_counter_inc(iface, RECV_HOST_REPLY);
	return 0;
}

/* process host queries and replies in a regular context, outside of interrupt context */
void
omx_process_host_queries_and_replies(void)
{
	struct sk_buff *in_skb;

	while ((in_skb = skb_dequeue(&omx_host_reply_list)) != NULL) {
		struct omx_hdr *mh;
		struct omx_pkt_head *ph;
		struct ethhdr *eh;
		struct omx_pkt_host_reply *reply_n;
		size_t hdr_len = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_host_reply);
		struct omx_peer *peer;
		int index;
		uint8_t new_hostnamelen;
		char *new_hostname;

		/* locate headers */
		mh = omx_skb_mac_header(in_skb);
		ph = &mh->head;
		eh = &ph->eth;
		reply_n = (struct omx_pkt_host_reply *) (ph + 1);

		index = OMX_FROM_PKT_FIELD(reply_n->return_peer_index);
		new_hostnamelen = OMX_FROM_PKT_FIELD(reply_n->length);
		new_hostname = kmalloc(new_hostnamelen, GFP_KERNEL);
		if (!new_hostname)
			goto done;
		skb_copy_bits(in_skb, hdr_len, new_hostname, new_hostnamelen);
		new_hostname[new_hostnamelen-1] = '\0';
		dprintk(QUERY, "got hostname %s from peer %d\n", new_hostname, index);

		mutex_lock(&omx_peers_mutex);
		peer = omx_peer_array[index];
		if (peer) {
			char *old_hostname = peer->hostname;

			if (!old_hostname) {
				list_del(&peer->host_query_list_elt);
				dprintk(PEER, "peer %s does not need host query anymore\n",
					new_hostname);
			}

			peer->hostname = new_hostname;
			kfree(old_hostname);
		}
		mutex_unlock(&omx_peers_mutex);

 done:
		kfree_skb(in_skb);
	}

	while ((in_skb = skb_dequeue(&omx_host_query_list)) != NULL) {
		struct omx_iface *iface = (void *) in_skb->sk;
		struct net_device * ifp = iface->eth_ifp;
		struct sk_buff *out_skb;
		struct omx_hdr *out_mh, *in_mh;
		struct omx_pkt_head *out_ph, *in_ph;
		struct ethhdr *out_eh, *in_eh;
		struct omx_pkt_host_query *query_n;
		struct omx_pkt_host_reply *reply_n;
		char * out_data;
		char *hostname;
		int hostnamelen;

		/* locate incoming headers */
		in_mh = omx_skb_mac_header(in_skb);
		in_ph = &in_mh->head;
		in_eh = &in_ph->eth;
		query_n = (struct omx_pkt_host_query *) (in_ph + 1);

		hostname = iface->peer.hostname;
		hostnamelen = strlen(hostname) + 1;

		/* prepare the reply */
		out_skb = omx_new_skb(ETH_ZLEN + hostnamelen);
		if (unlikely(out_skb == NULL)) {
			printk(KERN_INFO "Open-MX: Failed to create host reply skb\n");
	 		goto failed;
		}

		/* locate outgoing headers */
		out_mh = omx_skb_mac_header(out_skb);
		out_ph = &out_mh->head;
		out_eh = &out_ph->eth;
		reply_n = (struct omx_pkt_host_reply *) (out_ph + 1);
		out_data = (char *)(reply_n + 1);

		OMX_PKT_FIELD_FROM(reply_n->ptype, OMX_PKT_TYPE_HOST_REPLY);
		OMX_PKT_FIELD_FROM(reply_n->length, hostnamelen);
		reply_n->return_peer_index = query_n->return_peer_index;
		reply_n->magic = query_n->magic;
		memcpy(out_data, hostname, hostnamelen);

		dprintk(QUERY, "sending host reply with hostname %s\n", hostname);

		/* fill ethernet header, except the source for now */
        	out_eh->h_proto = __constant_cpu_to_be16(ETH_P_OMX);
		memcpy(out_eh->h_source, ifp->dev_addr, sizeof (out_eh->h_source));
		memcpy(out_eh->h_dest, in_eh->h_source, sizeof(out_eh->h_dest));

		out_skb->dev = ifp;
		dev_queue_xmit(out_skb);

	 failed:
		dev_kfree_skb(in_skb);
	}
}

void
omx_peers_clear_names(void)
{
	int i;

	mutex_lock(&omx_peers_mutex);
	for(i=0; i<omx_peer_max; i++) {
		struct omx_peer *peer;
		char *hostname;

		peer =  omx_peer_array[i];
		if (!peer || !peer->hostname || peer->local_iface)
			continue;

		hostname = peer->hostname;
		rcu_assign_pointer(peer->hostname, NULL);
		kfree(hostname);

		peer->host_query_last_resend_jiffies = 0;
		list_add_tail(&peer->host_query_list_elt, &omx_host_query_peer_list);
		dprintk(PEER, "peer needs host query\n");
	}
	mutex_unlock(&omx_peers_mutex);
}

/***********************
 * Peer Table Init/Exit
 */

int
omx_peers_init(void)
{
	int err;
	int i;

	skb_queue_head_init(&omx_host_query_list);
	skb_queue_head_init(&omx_host_reply_list);

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
	INIT_LIST_HEAD(&omx_host_query_peer_list);

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
	skb_queue_purge(&omx_host_query_list);
	skb_queue_purge(&omx_host_reply_list);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
