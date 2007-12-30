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
#include <linux/kref.h>

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
	char * hostname;

	rwlock_t endpoint_lock;
	enum omx_iface_status status;
	struct kref refcount;
	int endpoint_nr;
	struct omx_endpoint ** endpoints;

	uint32_t counters[OMX_COUNTER_INDEX_MAX];
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

	pid_t opener_pid;
	char opener_comm[TASK_COMM_LEN];

	rwlock_t lock;
	enum omx_endpoint_status status;
	struct kref refcount;
	struct list_head list_elt; /* the list entry for the cleanup list */

	struct omx_iface * iface;

	void * sendq, * recvq, * exp_eventq, * unexp_eventq;
	unsigned long next_exp_eventq_offset;
	unsigned long next_free_unexp_eventq_offset, next_reserved_unexp_eventq_offset;
	unsigned long next_recvq_offset;
	wait_queue_head_t waiters;
	spinlock_t event_lock;

	struct page ** sendq_pages;

	rwlock_t user_regions_lock;
	struct omx_user_region * user_regions[OMX_USER_REGION_MAX];

	rwlock_t pull_handle_lock;
	struct idr pull_handle_idr;
	struct list_head pull_handle_list;

	/* descriptor exported to user-space, modified by user-space and the driver,
	 * so we can export some info to user-space by writing into it, but we
	 * cannot rely on reading from it
	 */
	struct omx_endpoint_desc * userdesc;
};


/******************************
 * Notes about locking:
 *
 * The endpoint has 2 main status: FREE and OK. To prevent 2 people from changing it
 * at the same time, it is protected by a rwlock. To reduce the time we hold the lock,
 * there are 2 intermediate status: INITIALIZING and CLOSING.
 * When an endpoint is being used, its refcount is increased (by acquire/release)
 * When somebody wants to close an endpoint, it sets the CLOSING status (so that
 * new users can't acquire the endpoint), remove it from the interface, and the
 * the last user will release it for real.
 * The rwlock is taken as write only when opening and closing. Bottom halves are disabled
 * meanwhile since they might preempt the application. All other locks are taken as read,
 * especially on the receive side.
 *
 * The iface has both a kref to detect the last user and also has a number of endpoints 
 * attached to detect when we need to force.
 * There's a rwlock to protect this array against concurrent endpoint attach/detach.
 * When removing an iface (either by the user or by the netdevice notifier), the status
 * is set to CLOSING so that any new endpoint opener fails.
 * The rwlock is taken as write only when attach/detaching endpoints. Bottom halves are
 * disabled meanwhile since they might preempt the application. All other locks are taken
 * as read, especially on the receive side.
 *
 * When an iface is removed, all endpoints are scheduled for closing if necessary
 * (if forced) and the reference is released. The last endpoint will release the last
 * reference and thus release the device. When this happens because the unregister
 * notifier is called, the caller will wait for the last device reference to be released,
 * so we can return from the detach routine earlier as long as we guarantee that
 * things are being closed soon.
 *
 * The list of ifaces is always coherent since new ifaces are only added once initialized,
 * and removed in a coherent state (endpoints have been properly detached first)
 * Incoming packet processing is disabled while removing an iface.
 * So scanning the array of ifaces does not require locking,
 * but looking in the iface internals requires (read) locking.
 * The iface may not be removed while processing an incoming packet, so
 * we don't need locking and no need hold a reference on the iface either.
 * No need to disable bottom halves since it never scans the array of ifaces
 * (and the notifier callback may not be called from BH since it is interruptible).
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
	uint32_t id;

	rwlock_t lock;
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

struct omx_user_region_offset_state {
	int valid;
	unsigned long current_region_offset;
	unsigned long current_segment;
	unsigned long current_segment_offset;
};

#endif /* __omx_types_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
