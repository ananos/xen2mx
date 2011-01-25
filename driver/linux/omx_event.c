/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/rcupdate.h>

#include "omx_io.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_endpoint.h"

/*************************
 * Wait Queues and Wakeup
 */

struct omx_event_waiter {
	struct list_head list_elt;
	struct task_struct *task;
	struct rcu_head rcu_head;
	uint8_t status;
};

/* called with the endpoint event_lock read-held */
static INLINE void
omx_wakeup_waiter_list(struct omx_endpoint *endpoint,
		       uint32_t status)
{
	struct omx_event_waiter *waiter;

	/* wake up everybody with the event status */
	rcu_read_lock();
	list_for_each_entry_rcu(waiter, &endpoint->waiters, list_elt) {
		waiter->status = status;
		wake_up_process(waiter->task);
	}
	rcu_read_unlock();
}

static void
omx_wakeup_on_timeout_handler(unsigned long data)
{
	struct omx_event_waiter *waiter = (struct omx_event_waiter*) data;

	/* wakeup with the timeout status */
	waiter->status = OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT;
	wake_up_process(waiter->task);
}

static void
omx_wakeup_on_progress_timeout_handler(unsigned long data)
{
	struct omx_event_waiter *waiter = (struct omx_event_waiter*) data;

	/* wakeup with the progress status */
	waiter->status = OMX_CMD_WAIT_EVENT_STATUS_PROGRESS;
	wake_up_process(waiter->task);
}

/*****************
 * Initialization
 */

void
omx_endpoint_queues_init(struct omx_endpoint *endpoint)
{
	union omx_evt * evt;

	/* sanity checks */
	BUILD_BUG_ON(PAGE_SIZE%OMX_SENDQ_ENTRY_SIZE != 0 && OMX_SENDQ_ENTRY_SIZE%PAGE_SIZE != 0);
	BUILD_BUG_ON(PAGE_SIZE%OMX_RECVQ_ENTRY_SIZE != 0 && OMX_RECVQ_ENTRY_SIZE%PAGE_SIZE != 0);
	BUILD_BUG_ON(sizeof(union omx_evt) != OMX_EVENTQ_ENTRY_SIZE);
	BUILD_BUG_ON(OMX_UNEXP_EVENTQ_ENTRY_NR != OMX_RECVQ_ENTRY_NR);

	/* initialize all expected events */
	for(evt = endpoint->exp_eventq;
	    (void *) evt < endpoint->exp_eventq + OMX_EXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.id = 0;

	/* initialize indexes */
	endpoint->nextfree_exp_eventq_index = 0;
	endpoint->nextreleased_exp_eventq_index = 0;
	BUILD_BUG_ON((omx_eventq_index_t) -1 <= OMX_EXP_EVENTQ_ENTRY_NR);

	/* initialize all unexpected events */
	for(evt = endpoint->unexp_eventq;
	    (void *) evt < endpoint->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.id = 0;

	/* set the first free and reserved unexpected event slot */
	endpoint->nextfree_unexp_eventq_index = 0;
	endpoint->nextreserved_unexp_eventq_index = 0;
	endpoint->nextreleased_unexp_eventq_index = 0;
	BUILD_BUG_ON((omx_eventq_index_t) -1 <= OMX_UNEXP_EVENTQ_ENTRY_NR);

	/* set the first recvq slot */
	endpoint->next_recvq_offset = 0;

	INIT_LIST_HEAD(&endpoint->waiters);
	spin_lock_init(&endpoint->event_lock);
	spin_lock_init(&endpoint->release_exp_lock);
	spin_lock_init(&endpoint->release_unexp_lock);
}

/******************************************
 * Report an expected event to users-space
 */

int
omx_notify_exp_event(struct omx_endpoint *endpoint, const void *event, int length)
{
	union omx_evt *slot;
	omx_eventq_index_t index;

	spin_lock_bh(&endpoint->event_lock);

	if (unlikely(endpoint->nextfree_exp_eventq_index - endpoint->nextreleased_exp_eventq_index
		     >= OMX_EXP_EVENTQ_ENTRY_NR)) {
		/* the application sucks, it did not check
		 * the expected eventq before posting requests
		 */
		dprintk(EVENT,
			"Open-MX: Expected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, EXP_EVENTQ_FULL);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_EXP_EVENTQ_FULL;
		spin_unlock_bh(&endpoint->event_lock);
		return -EBUSY;
	}

	/* take the next slot and update the queue */
	index = endpoint->nextfree_exp_eventq_index++;

	slot = endpoint->exp_eventq + (index % OMX_EXP_EVENTQ_ENTRY_NR) * OMX_EVENTQ_ENTRY_SIZE;

	/* store the event without setting the id first */
	memcpy(slot, event, length);
	wmb();
	/* write the actual id now that the whole event has been written to memory */
	((struct omx_evt_generic *) slot)->id = 1 + (index % OMX_EVENT_ID_MAX);

	/* wake up waiters */
	dprintk(EVENT, "notify_exp waking up everybody\n");

	spin_unlock_bh(&endpoint->event_lock);

	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_EVENT);

	return 0;
}

/********************************************
 * Report an unexpected event to users-space
 * without any recvq slot needed
 */

int
omx_notify_unexp_event(struct omx_endpoint *endpoint, const void *event, int length)
{
	union omx_evt *slot;
	omx_eventq_index_t index;

	spin_lock_bh(&endpoint->event_lock);

	if (unlikely(endpoint->nextfree_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index
		     >= OMX_UNEXP_EVENTQ_ENTRY_NR)) {
		/* the application did not process the unexpected queue and release slots fast enough */
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, UNEXP_EVENTQ_FULL);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL;
		spin_unlock_bh(&endpoint->event_lock);
		return -EBUSY;
	}

	/* take the next slot and update the queue */
	endpoint->nextfree_unexp_eventq_index++;

	index = endpoint->nextreserved_unexp_eventq_index++;
	slot = endpoint->unexp_eventq + (index % OMX_UNEXP_EVENTQ_ENTRY_NR) * OMX_EVENTQ_ENTRY_SIZE;

	/* store the event without setting the id first */
	memcpy(slot, event, length);
	wmb();
	/* write the actual id now that the whole event has been written to memory */
	((struct omx_evt_generic *) slot)->id = 1 + (index % OMX_EVENT_ID_MAX);

	/* wake up waiters */
	dprintk(EVENT, "notify_unexp waking up everybody\n");

	spin_unlock_bh(&endpoint->event_lock);

	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_EVENT);

	return 0;
}

/********************************************
 * Report an unexpected event to users-space
 * with a recvq slot needed
 */

/*
 * The recvq accounting is trivial since there are as many recvq slots
 * than unexp event slots, the latter are accounted, and we allocate only
 * one recvq slot per prepare()/commit() functions below (and no slot
 * in notify() above).
 */

/* Reserve one more slot and returns the corresponding recvq slot to the caller */
int
omx_prepare_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint,
					  unsigned long *recvq_offset_p)
{
	spin_lock_bh(&endpoint->event_lock);

	if (unlikely(endpoint->nextfree_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index
		     >= OMX_UNEXP_EVENTQ_ENTRY_NR)) {
		/* the application did not process the unexpected queue and release slots fast enough */
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, UNEXP_EVENTQ_FULL);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL;
		spin_unlock_bh(&endpoint->event_lock);
		return -EBUSY;
	}

	/* reserve the next slot and update the queue */
	endpoint->nextfree_unexp_eventq_index++;

	/* take the next recvq slot and return it now */
	*recvq_offset_p = endpoint->next_recvq_offset;
	endpoint->next_recvq_offset += OMX_RECVQ_ENTRY_SIZE;
	if (unlikely(endpoint->next_recvq_offset >= OMX_RECVQ_SIZE))
		/* all slots have the same size, so there can't be a slot that wraps around the end */
		endpoint->next_recvq_offset = 0;

	spin_unlock_bh(&endpoint->event_lock);
	return 0;
}

/* Reserve nr more slots and returns the corresponding recvq slots to the caller */
int
omx_prepare_notify_unexp_events_with_recvq(struct omx_endpoint *endpoint,
					   int nr,
					   unsigned long *recvq_offset_p)
{
	int i;

	spin_lock_bh(&endpoint->event_lock);

	if (unlikely(endpoint->nextfree_unexp_eventq_index + nr - 1 - endpoint->nextreleased_unexp_eventq_index
		     >= OMX_UNEXP_EVENTQ_ENTRY_NR)) {
		/* the application did not process the unexpected queue and release slots fast enough */
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, UNEXP_EVENTQ_FULL);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL;
		spin_unlock_bh(&endpoint->event_lock);
		return -EBUSY;
	}

	/* reserve the next slots and update the queue */
	endpoint->nextfree_unexp_eventq_index += nr;

	/* take the next recvq slots and return them now */
	for(i=0; i<nr; i++) {
		recvq_offset_p[i] = endpoint->next_recvq_offset;
		endpoint->next_recvq_offset += OMX_RECVQ_ENTRY_SIZE;
		if (unlikely(endpoint->next_recvq_offset >= OMX_RECVQ_SIZE))
			/* all slots have the same size, so there can't be a slot that wraps around the end */
			endpoint->next_recvq_offset = 0;
	}

	spin_unlock_bh(&endpoint->event_lock);
	return 0;
}

/*
 * Store the event in the next reserved slot
 * (not always the one reserved during omx_commit_notify_unexp_event()
 *  since prepare/commit calls could have been overlapped).
 */
void
omx_commit_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint,
					 const void *event, int length)
{
	union omx_evt *slot;
	omx_eventq_index_t index;

	spin_lock_bh(&endpoint->event_lock);

	/* the caller should have called prepare() earlier */
	BUG_ON(endpoint->nextreserved_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index
	       >= endpoint->nextfree_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index);

	/* update the next reserved slot in the queue */
	index = endpoint->nextreserved_unexp_eventq_index++;
	slot = endpoint->unexp_eventq + (index % OMX_UNEXP_EVENTQ_ENTRY_NR) * OMX_EVENTQ_ENTRY_SIZE;

	/* store the event without setting the id first */
	memcpy(slot, event, length);
	wmb();
	/* write the actual id now that the whole event has been written to memory */
	((struct omx_evt_generic *) slot)->id = 1 + (index % OMX_EVENT_ID_MAX);

	/* wake up waiters */
	dprintk(EVENT, "commit_notify_unexp waking up everybody\n");

	spin_unlock_bh(&endpoint->event_lock);

	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_EVENT);
}

/*
 * Store an dummy "ignored" event in the next reserved slot
 * (not always the one reserved during omx_commit_notify_unexp_event()
 *  since prepare/commit calls could have been overlapped).
 * We can't cancel for real since the recvq slot could not be the last one.
 */
void
omx_cancel_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint)
{
	union omx_evt *slot;
	omx_eventq_index_t index;

	spin_lock_bh(&endpoint->event_lock);

	/* the caller should have called prepare() earlier */
	BUG_ON(endpoint->nextreserved_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index
	       >= endpoint->nextfree_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index);

	/* update the next reserved slot in the queue */
	index = endpoint->nextreserved_unexp_eventq_index++;
	slot = endpoint->unexp_eventq + (index % OMX_UNEXP_EVENTQ_ENTRY_NR) * OMX_EVENTQ_ENTRY_SIZE;

	/* store the event without setting the id first */
	((struct omx_evt_generic *) slot)->id = 0;
	((struct omx_evt_generic *) slot)->type = OMX_EVT_IGNORE;
	wmb();
	/* write the actual id now that the whole event has been written to memory */
	((struct omx_evt_generic *) slot)->id = 1 + (index % OMX_EVENT_ID_MAX);

	/* no need to wakeup people */

	spin_unlock_bh(&endpoint->event_lock);
}

/***********
 * Sleeping
 */

static void
__omx_event_waiter_rcu_free_callback(struct rcu_head *rcu_head)
{
	struct omx_event_waiter * waiter = container_of(rcu_head, struct omx_event_waiter, rcu_head);
	kfree(waiter);
}

/* FIXME: this is for when the application waits, not when the progression thread does */
int
omx_ioctl_wait_event(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct omx_cmd_wait_event cmd;
	struct omx_event_waiter * waiter;
	struct timer_list timer;
	int err = 0;

	/* lib-progression-requested timeout */
	uint64_t wakeup_jiffies = endpoint->userdesc->wakeup_jiffies;

	/* timer, either from the ioctl or from the lib-progression-requested timeout */
	void (*timer_handler)(unsigned long) = NULL;
	uint64_t timer_jiffies = 0;

	/* cache current jiffies */
	u64 current_jiffies;

	err = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read wait event cmd hdr\n");
		err = -EFAULT;
		goto out;
	}

	waiter = kmalloc(sizeof(struct omx_event_waiter), GFP_KERNEL);
	if (!waiter) {
		printk(KERN_ERR "Open-MX: failed to allocate waiter");
		err = -ENOMEM;
		goto out;
	}

	/* FIXME: wait on some event type only */

	spin_lock_bh(&endpoint->event_lock);

	/* queue ourself on the wait queue first, in case a packet arrives in the meantime */
	list_add_tail_rcu(&waiter->list_elt, &endpoint->waiters);
	waiter->status = OMX_CMD_WAIT_EVENT_STATUS_NONE;
	waiter->task = current;
	set_current_state(TASK_INTERRUPTIBLE);

	/* did we deposit an event before the lib decided to go to sleep ? */
	BUILD_BUG_ON(sizeof(cmd.next_exp_event_index) != sizeof(endpoint->nextfree_exp_eventq_index));
	BUILD_BUG_ON(sizeof(cmd.next_unexp_event_index) != sizeof(endpoint->nextreserved_unexp_eventq_index));
	BUILD_BUG_ON(sizeof(cmd.user_event_index) != sizeof(endpoint->userdesc->user_event_index));
	if (cmd.next_exp_event_index != endpoint->nextfree_exp_eventq_index
	    || cmd.next_unexp_event_index != endpoint->nextreserved_unexp_eventq_index
	    || cmd.user_event_index != endpoint->userdesc->user_event_index) {
		dprintk(EVENT, "wait event race (%ld,%ld,%ld) != (%ld,%ld,%ld)\n",
			(unsigned long) cmd.next_exp_event_index,
			(unsigned long) cmd.next_unexp_event_index,
			(unsigned long) cmd.user_event_index,
			(unsigned long) endpoint->nextfree_exp_eventq_index,
			(unsigned long) endpoint->nextreserved_unexp_eventq_index,
			(unsigned long) endpoint->userdesc->user_event_index);
		spin_unlock_bh(&endpoint->event_lock);
		cmd.status = OMX_CMD_WAIT_EVENT_STATUS_RACE;
		goto wakeup;
	}

	spin_unlock_bh(&endpoint->event_lock);

	/* setup the timer if needed by an application timeout */
	if (cmd.jiffies_expire != OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE) {
		timer_handler = omx_wakeup_on_timeout_handler;
		timer_jiffies = cmd.jiffies_expire;
	}
	/* setup the timer if needed by a progress timeout */
	if (wakeup_jiffies != OMX_NO_WAKEUP_JIFFIES
	    && (!timer_handler || time_before64(wakeup_jiffies, timer_jiffies))) {
		timer_handler = omx_wakeup_on_progress_timeout_handler;
		timer_jiffies = wakeup_jiffies;
	}

	/* cache jiffies for multiple later use */
	current_jiffies = get_jiffies_64();

	/* setup the timer for real now */
	if (timer_handler) {
		/* check timer races */
		if (time_after_eq64(current_jiffies, timer_jiffies)) {
			dprintk(EVENT, "wait event expire %lld has passed (now is %lld), not sleeping\n",
				(unsigned long long) cmd.jiffies_expire, (unsigned long long) current_jiffies);
			waiter->status = OMX_CMD_WAIT_EVENT_STATUS_RACE;
			goto wakeup;
		}
		setup_timer(&timer, timer_handler, (unsigned long) waiter);
		/* timer not pending yet, use the regular mod_timer() */
		mod_timer(&timer, timer_jiffies);
		dprintk(EVENT, "wait event timer setup at %lld (now is %lld)\n",
			(unsigned long long) timer_jiffies, (unsigned long long) current_jiffies);
	}

	if (waiter->status == OMX_CMD_WAIT_EVENT_STATUS_NONE
	    && !signal_pending(current)) {
		/* if nothing happened, let's go to sleep */
		dprintk(EVENT, "going to sleep at %lld\n", (unsigned long long) current_jiffies);
		schedule();
		dprintk(EVENT, "waking up from sleep at %lld\n", (unsigned long long) current_jiffies);

	} else {
		/* already "woken-up", no need to sleep */
		dprintk(EVENT, "not going to sleep, status is already %d\n", (unsigned) waiter->status);
	}

	/* remove the timer */
	if (timer_handler)
		del_singleshot_timer_sync(&timer);

 wakeup:
	__set_current_state(TASK_RUNNING); /* no need to serialize with below, __set is enough */

	spin_lock_bh(&endpoint->event_lock);
	list_del_rcu(&waiter->list_elt);
	spin_unlock_bh(&endpoint->event_lock);

	if (waiter->status == OMX_CMD_WAIT_EVENT_STATUS_NONE) {
		/* status didn't changed, we have been interrupted */
		waiter->status = OMX_CMD_WAIT_EVENT_STATUS_INTR;
	}

	cmd.status = waiter->status;

	call_rcu(&waiter->rcu_head, __omx_event_waiter_rcu_free_callback);

	err = copy_to_user(uparam, &cmd, sizeof(cmd));
	if (unlikely(err != 0)) {
		err = -EFAULT;
		printk(KERN_ERR "Open-MX: Failed to write wait event cmd result\n");
	}

	return 0;

 out:
	return err;
}

int
omx_ioctl_release_exp_slots(struct omx_endpoint *endpoint, void __user *uparam)
{
	int err = 0;
	spin_lock(&endpoint->release_exp_lock);
	if (endpoint->nextfree_exp_eventq_index - endpoint->nextreleased_exp_eventq_index
	    < OMX_EXP_RELEASE_SLOTS_BATCH_NR)
		err = -EINVAL;
	else
		endpoint->nextreleased_exp_eventq_index += OMX_EXP_RELEASE_SLOTS_BATCH_NR;
	spin_unlock(&endpoint->release_exp_lock);
	return err;
}

int
omx_ioctl_release_unexp_slots(struct omx_endpoint *endpoint, void __user *uparam)
{
	int err = 0;
	spin_lock(&endpoint->release_unexp_lock);
	if (endpoint->nextreserved_unexp_eventq_index - endpoint->nextreleased_unexp_eventq_index
	    < OMX_UNEXP_RELEASE_SLOTS_BATCH_NR)
		err = -EINVAL;
	else
		endpoint->nextreleased_unexp_eventq_index += OMX_UNEXP_RELEASE_SLOTS_BATCH_NR;
	spin_unlock(&endpoint->release_unexp_lock);
	return err;
}

int
omx_ioctl_wakeup(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct omx_cmd_wakeup cmd;
	int err;

	err = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read wakeup cmd hdr\n");
		err = -EFAULT;
		goto out;
	}

	omx_wakeup_waiter_list(endpoint, cmd.status);

	return 0;

 out:
	return err;
}

void
omx_wakeup_endpoint_on_close(struct omx_endpoint * endpoint)
{
	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_WAKEUP);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
