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

#ifndef __omx_xenback_h__
#define __omx_xenback_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_reg.h"

#define OMX_XEN_BACKEND_TIMEOUT 1000 * 1000

#include "omx_xen_timers.h"
#include "omx_xen.h"

enum backend_status {
        OMX_XEN_BACKEND_STATUS_DONE,
        OMX_XEN_BACKEND_STATUS_DOING,
        OMX_XEN_BACKEND_STATUS_FAILED,
};

typedef struct omx_xenif_st {
	/* Unique identifier for this interface. */
	domid_t domid;
	unsigned int handle;
	unsigned int irq;
	unsigned int evtchn;
	/* Back pointer to the backend_info. */
	struct backend_info *be;
	/* Private fields. */
	spinlock_t omx_be_lock;
	spinlock_t omx_send_lock;
	spinlock_t omx_resp_lock;
	spinlock_t omx_recv_ring_lock;
	spinlock_t omx_ring_lock;
	atomic_t refcnt;

	wait_queue_head_t wq;
	wait_queue_head_t resp_wq;
	wait_queue_head_t waiting_to_free;

	struct task_struct *task;
	struct workqueue_struct *msg_workq;
	struct work_struct msg_workq_task;
	struct workqueue_struct *response_msg_workq;
	struct work_struct response_workq_task;
	struct completion completion;

	grant_handle_t recv_handle;
	grant_ref_t recv_ref;
	grant_handle_t shmem_handle;
	grant_ref_t shmem_ref;

	unsigned long st_print;

	unsigned int card_index;
	struct omx_xenif_back_ring ring;
	struct vm_struct *omx_xenif_ring_area;
	struct omx_xenif_back_ring recv_ring;
	struct vm_struct *recv_ring_area;
        enum backend_status status;
        spinlock_t status_lock;
	uint32_t recvq_offset;
	uint32_t sendq_offset;


#ifdef OMX_XEN_COOKIES
        struct list_head page_cookies_free;
        rwlock_t page_cookies_freelock;

        struct list_head page_cookies_inuse;
        rwlock_t page_cookies_inuselock;
#endif

} omx_xenif_t;

struct omx_xen_user_region {
	uint32_t id;
	uint32_t eid;

        unsigned dirty : 1;
        struct kref refcount;
        struct omx_endpoint *endpoint;

        struct rcu_head rcu_head; /* rcu deferred releasing callback */
        int nr_vmalloc_segments;
        struct work_struct destroy_work;

        unsigned nr_segments;
        unsigned long total_length;

        enum omx_user_region_status status;
        unsigned long total_registered_length;

	struct omx_xen_user_region_segment {
		uint32_t sid;
		unsigned long nr_pages;
		unsigned long aligned_vaddr;
		unsigned long length;
		unsigned long pinned_pages;
		unsigned first_page_offset;
		int vmalloced;
		unsigned long *vaddrs;
		unsigned long all_gref[OMX_XEN_GRANT_PAGES_MAX];
		unsigned long all_handle[OMX_XEN_GRANT_PAGES_MAX];
		struct vm_struct *vm_gref[OMX_XEN_GRANT_PAGES_MAX];
		grant_handle_t *handles;
		uint8_t nr_parts;
		//struct gnttab_map_grant_ref **map;
		//struct gnttab_unmap_grant_ref **unmap;
		uint32_t **gref_list;
#ifdef OMX_XEN_COOKIES
		struct omx_xen_page_cookie **cookies;
#endif
		uint16_t gref_offset;
		struct page **pages;
	} segments[0];
};

struct omxback_dev {
	uint8_t id;
	struct omx_endpoint *endpoints[OMX_XEN_MAX_ENDPOINTS];

};

struct backend_info {
	struct xenbus_device *dev;
	long int frontend_id;
	enum xenbus_state frontend_state;
	struct xenbus_watch backend_watch;
	struct xenbus_watch watch;
	struct omxback_dev *omxdev;
	omx_xenif_t *omx_xenif;
	spinlock_t lock;

	int remoteDomain;
	int gref;
	unsigned long all_gref;
	int irq;
	struct evtchn_alloc_unbound evtchn;
	//struct omx_xenif_back_ring ring;
	char *frontpath;
};

int omx_xenback_init(void);
void omx_xenback_exit(void);

void msg_workq_handler(struct work_struct *work);
void response_workq_handler(struct work_struct *work);

int omx_poke_domU(omx_xenif_t *omx_xenif, struct omx_xenif_response *ring_resp);

irqreturn_t omx_xenif_be_int(int irq, void *data);

extern timers_t t_recv, t_rndv, t_notify, t_small, t_tiny, t_medium, t_connect, t_truc;
extern timers_t t_pull_request, t_pull_reply, t_pull, t_handle;
extern timers_t t_send_tiny, t_send_small, t_send_medium, t_send_connect, t_send_notify, t_send_connect_reply, t_send_rndv, t_send_liback;
extern timers_t t_create_reg, t_reg_seg, t_destroy_reg, t_dereg_seg;

#endif				/* __omx_xenback_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
