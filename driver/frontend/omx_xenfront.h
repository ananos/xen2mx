/*
 * Xen2MX
 * Copyright © Anastassios Nanos 2012
 * (see AUTHORS file)
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

#ifndef __omx_xenfront_h__
#define __omx_xenfront_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>

#include "omx_xen_timers.h"
#include "omx_xen.h"

enum frontend_status {
	OMX_XEN_FRONTEND_STATUS_DONE,
	OMX_XEN_FRONTEND_STATUS_DOING,
	OMX_XEN_FRONTEND_STATUS_FAILED,
};
struct omx_xenfront_info {
	struct list_head list;
	uint16_t handle;
	struct xenbus_device *xbdev;
	struct omx_xenif_front_ring ring;
	struct omx_xenif_front_ring recv_ring;
	grant_ref_t gref;
	int ring_ref;
	int recv_ring_ref;
	struct evtchn_bind_interdomain evtchn;
	unsigned int evtchn2, irq;
	enum omx_xenif_state connected;
	uint8_t is_ready;
	spinlock_t lock;
	spinlock_t msg_handler_lock;
	struct omx_endpoint *endpoints[OMX_XEN_MAX_ENDPOINTS];
	uint32_t board_count;
	struct omx_cmd_peer_table_state state;
	struct omx_board_info board_info;
	struct omx_cmd_misc_peer_info peer_info;
	enum frontend_status status;
	spinlock_t status_lock;
	wait_queue_head_t wq;

        struct list_head gref_cookies_free;
        rwlock_t gref_cookies_freelock;

        struct list_head gref_cookies_inuse;
        rwlock_t gref_cookies_inuselock;


	struct task_struct *task;
	struct workqueue_struct *msg_workq;
	struct work_struct msg_workq_task;

};

struct omx_xenfront_dev {
	struct cdev cdev;
	spinlock_t endpoint_lock;
	struct omx_xenfront_info *fe;
};

#if 0
struct omx_viface {
	int index;

	struct net_device *eth_ifp;
	struct omx_peer peer;
	uint32_t *reverse_peer_indexes;	/* our index in the remote peer tables, or OMX_UNKNOWN_REVERSE_PEER_INDEX
					   (omx_peer_max values) */

	struct mutex endpoints_mutex;
	enum omx_iface_status status;
	struct kref refcount;
	int endpoint_nr;
	struct omx_endpoint __rcu **endpoints;
	struct omx_iface_raw raw;

	uint32_t counters[OMX_COUNTER_INDEX_MAX];
};
#endif
int omx_xenfront_init(void);
void omx_xenfront_exit(void);

int omx_ioctl_xen_user_region_create(struct omx_endpoint *endpoint,
				     void __user * uparam);
int omx_ioctl_xen_user_region_destroy(struct omx_endpoint *endpoint,
				      void __user * uparam);
int omx_xen_user_region_release(struct omx_endpoint *endpoint,
				uint32_t region_id);
int omx_ioctl_xen_get_board_info(struct omx_endpoint *endpoint,
				 void __user * uparam);

int omx_poke_dom0(struct omx_xenfront_info *fe, struct omx_xenif_request *ring_req);

int wait_for_backend_response(unsigned int *poll_var, unsigned int status,
			      spinlock_t * spin);

int omx_xen_endpoint_get_info(uint32_t board_index, uint32_t endpoint_index,
			      struct omx_endpoint_info *info);

int omx_xen_peer_lookup(uint32_t * index, uint64_t * board_addr, char *hostname,
			uint32_t cmd);

extern struct omx_xenfront_info *__omx_xen_frontend;

void omx_xenif_interrupt(struct work_struct *work);
void omx_xenif_interrupt_recv(struct work_struct *work);

int omx_xen_peer_table_get_state(struct omx_cmd_peer_table_state *state);
int omx_xen_peer_table_set_state(struct omx_cmd_peer_table_state *state);
int omx_xen_ifaces_get_count(uint32_t *count);
int omx_xen_set_hostname(uint32_t board_index, const char *hostname);

//extern timers_t t_recv, t_rndv, t_notify, t_small, t_tiny, t_medium, t_connect, t_truc;
//extern timers_t t_pull_request, t_pull_reply, t_pull, t_handle;
//extern timers_t t_send_tiny, t_send_small, t_send_medium, t_send_connect, t_send_notify, t_send_connect_reply, t_send_rndv, t_send_liback;


extern timers_t t_create_reg, t_destroy_reg, t_reg_seg, t_dereg_seg;
extern timers_t t_pull;
extern timers_t t_send_tiny, t_send_small, t_send_mediumva,
    t_send_mediumsq_frag, t_send_connect_request, t_send_notify,
    t_send_connect_reply, t_send_rndv, t_send_liback;
#endif				/* __omx_xenfront_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
