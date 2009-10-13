/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2009 (see AUTHORS file)
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

#ifndef __omx_iface_h__
#define __omx_iface_h__

#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/moduleparam.h>
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#ifdef OMX_HAVE_MUTEX
#include <linux/mutex.h>
#endif

#include "omx_io.h"
#include "omx_hal.h"
#include "omx_peer.h"

struct omx_endpoint;

enum omx_iface_status {
	/* iface is ready to be used */
	OMX_IFACE_STATUS_OK,
	/* iface is being closed by somebody else, no new endpoint may be open */
	OMX_IFACE_STATUS_CLOSING,
};

struct omx_iface_raw {
	struct file *opener_file;
	pid_t opener_pid;
	char opener_comm[TASK_COMM_LEN];

	struct list_head event_list;
	spinlock_t event_lock;
	wait_queue_head_t event_wq;
	int event_list_length;
};

struct omx_iface {
	int index;

	struct net_device * eth_ifp;
	struct omx_peer peer;
	uint32_t *reverse_peer_indexes; /* our index in the remote peer tables, or OMX_UNKNOWN_REVERSE_PEER_INDEX (omx_peer_max values) */

	struct mutex endpoints_mutex;
	enum omx_iface_status status;
	struct kref refcount;
	int endpoint_nr;
	struct omx_endpoint ** endpoints;
	struct omx_iface_raw raw;

	uint32_t counters[OMX_COUNTER_INDEX_MAX];
};

extern int omx_net_init(void);
extern void omx_net_exit(void);

extern struct omx_iface ** omx_ifaces;

extern void omx_iface_release(struct omx_iface * iface);

/*
 * Take another reference on an iface.
 * Must be called either when holding the ifaces array lock,
 * from a RCU read section, or when holding another reference
 * on the same iface
 */
static inline void
omx_iface_reacquire(struct omx_iface * iface)
{
	kref_get(&iface->refcount);
}

extern struct omx_iface * omx_iface_find_by_index_lock(int board_index);

extern void omx_for_each_iface(int (*handler)(struct omx_iface *iface, void *data), void *data);
extern void omx_for_each_endpoint(int (*handler)(struct omx_endpoint *endpoint, void *data), void *data);
extern void omx_for_each_endpoint_in_mm(struct mm_struct *mm, int (*handler)(struct omx_endpoint *endpoint, void *data), void *data);

extern int omx_ifnames_get(char *buf, size_t buflen, char sep);
extern int omx_ifnames_get_kp(char *buf, struct kernel_param *kp);
extern int omx_ifnames_set_kp(const char *buf, struct kernel_param *kp);

extern int omx_ifaces_get_count(void);
extern int omx_iface_get_info(uint32_t board_index, struct omx_board_info *info);
extern struct omx_iface * omx_iface_find_by_ifp(const struct net_device *ifp);
extern struct omx_iface * omx_iface_find_by_addr(uint64_t addr);
extern int omx_iface_get_counters(uint32_t board_index, int clear, uint64_t buffer_addr, uint32_t buffer_length);
extern int omx_iface_set_hostname(uint32_t board_index, const char * hostname);
extern int omx_iface_get_rx_coalesce(struct net_device * ifp, unsigned *usecs);

extern void omx__raw_detach_iface_locked(struct omx_iface *iface);

extern struct omx_iface * omx_shared_fake_iface;

/* counters */
#if defined(OMX_DRIVER_COUNTERS)
#  define omx_counter_inc(iface, index)		\
do {						\
	iface->counters[OMX_COUNTER_##index]++;	\
} while (0)
#else
#  define omx_counter_inc(iface, index) (void) iface /* to silence unused warning */
#endif /* OMX_DRIVER_COUNTERS */

#endif /* __omx_iface_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
