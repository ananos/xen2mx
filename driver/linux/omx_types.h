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

#ifndef __omx_types_h__
#define __omx_types_h__

#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/idr.h>

#include "omx_wire.h"
#include "omx_io.h"

enum omx_iface_status {
	/* iface is ready to be used */
	OMX_IFACE_STATUS_OK,
	/* iface is being closed by somebody else, no new endpoint may be open */
	OMX_IFACE_STATUS_CLOSING,
};

struct omx_iface {
	int index;

	struct net_device * eth_ifp;
	char * board_name;

	spinlock_t endpoint_lock;
	int endpoint_nr;
	struct omx_endpoint ** endpoints;
	wait_queue_head_t noendpoint_queue;

	enum omx_iface_status status;
};

enum omx_endpoint_status {
	/* endpoint is free and may be open */
	OMX_ENDPOINT_STATUS_FREE,
	/* endpoint is already being open by somebody else */
	OMX_ENDPOINT_STATUS_INITIALIZING,
	/* endpoint is ready to be used */
	OMX_ENDPOINT_STATUS_OK,
	/* endpoint is being closed by somebody else */
	OMX_ENDPOINT_STATUS_CLOSING,
};

struct omx_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;
	uint32_t session_id;

	spinlock_t lock;
	enum omx_endpoint_status status;
	atomic_t refcount;
	wait_queue_head_t noref_queue;

	struct omx_iface * iface;

	void * sendq, * recvq, * eventq;
	union omx_evt * next_eventq_slot;
	char * next_recvq_slot;

	spinlock_t user_regions_lock;
	struct omx_user_region * user_regions[OMX_USER_REGION_MAX];

	spinlock_t pull_handle_lock;
	struct idr pull_handle_idr;
	struct list_head pull_handle_list;
};


/******************************
 * Notes about locking:
 *
 * The endpoint has 2 main status: FREE and OK. To prevent 2 people from changing it
 * at the same time, it is protected by a lock. To reduce the time we hold the lock,
 * there are 2 intermediate status: INITIALIZING and CLOSING.
 * When an endpoint is being used, its refcount is increased (by acquire/release)
 * When somebody wants to close an endpoint, it sets the CLOSING status (so that
 * new users can't acquire the endpoint) and waits for current users to release
 * (when refcount becomes 0).
 *
 * The iface doesn't have an actual refcount since it has a number of endpoints attached.
 * There's a lock to protect this array against concurrent endpoint attach/detach.
 * When removing an iface (either by the user or by the netdevice notifier), the status
 * is set to CLOSING so that any new endpoint opener fails.
 *
 * The list of ifaces is always coherent since new ifaces are only added once initialized,
 * and removed in a coherent state (endpoints have been properly detached first)
 * Incoming packet processing is disabled while removing an iface.
 * So scanning the array of ifaces does not require locking,
 * but looking in the iface internals requires locking.
 * The iface may not be removed while processing an incoming packet, so
 * we don't need locking and no need hold a reference on the iface either.
 *
 * The locks are always taken in this priority order:
 * omx_iface_lock, iface->endpoint_lock, endpoint->lock
 */

enum omx_user_region_status {
	/* region is ready to be used */
	OMX_USER_REGION_STATUS_OK,
	/* region is being closed by somebody else */
	OMX_USER_REGION_STATUS_CLOSING,
};

struct omx_user_region {
	spinlock_t lock;
	enum omx_user_region_status status;
	atomic_t refcount;
	wait_queue_head_t noref_queue;

	unsigned nr_segments;
	unsigned long total_length;
	struct omx_user_region_segment {
		unsigned first_page_offset;
		unsigned long length;
		unsigned long nr_pages;
		struct page ** pages;
	} segments[0];
};

#endif /* __omx_types_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
