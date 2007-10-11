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

#include "omx_io.h"
#include "omx_common.h"

/*****************
 * Initialization
 */

void
omx_endpoint_queues_init(struct omx_endpoint *endpoint)
{
	union omx_evt * evt;

	/* initialize all expected events to none */
	for(evt = endpoint->exp_eventq;
	    (void *) evt < endpoint->exp_eventq + OMX_EXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = OMX_EVT_NONE;
	/* set the first expected event slot */
	endpoint->next_exp_eventq_offset = 0;

	/* initialize all unexpected events to none */
	for(evt = endpoint->unexp_eventq;
	    (void *) evt < endpoint->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = OMX_EVT_NONE;
	/* set the first free and reserved unexpected event slot */
	endpoint->next_free_unexp_eventq_offset = 0;
	endpoint->next_reserved_unexp_eventq_offset = 0;

	/* set the first recvq slot */
	endpoint->next_recvq_slot = endpoint->recvq;

	spin_lock_init(&endpoint->event_lock);
}

/******************************************
 * Report an expected event to users-space
 */

int
omx_notify_exp_event(struct omx_endpoint *endpoint,
		     uint8_t type, void *event, int length)
{
	union omx_evt *slot;

	spin_lock(&endpoint->event_lock);

	slot = endpoint->exp_eventq + endpoint->next_exp_eventq_offset;
	if (unlikely(slot->generic.type != OMX_EVT_NONE)) {
		/* the application sucks, it did not check
		 * the expected eventq before posting requests
		 */
		dprintk(EVENT,
			"Open-MX: Expected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		spin_unlock(&endpoint->event_lock);
		return -EBUSY;
	}

	/* update the queue */
	endpoint->next_exp_eventq_offset += OMX_EVENTQ_ENTRY_SIZE;
	if (unlikely(endpoint->next_exp_eventq_offset >= OMX_EXP_EVENTQ_SIZE))
		endpoint->next_exp_eventq_offset = 0;

	spin_unlock(&endpoint->event_lock);

	/* store the event */
	memcpy(slot, event, length);
	wmb();
	((struct omx_evt_generic *) slot)->type = type;

	return 0;
}

/********************************************
 * Report an unexpected event to users-space
 * without any recvq slot needed
 */

int
omx_notify_unexp_event(struct omx_endpoint *endpoint,
		       uint8_t type, void *event, int length)
{
	union omx_evt *slot;

	spin_lock(&endpoint->event_lock);

	slot = endpoint->unexp_eventq + endpoint->next_free_unexp_eventq_offset;
	if (unlikely(slot->generic.type != OMX_EVT_NONE)) {
		/* the application sucks, it did not check
		 * the unexpected eventq before posting requests
		 */
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		spin_unlock(&endpoint->event_lock);
		return -EBUSY;
	}

	/* update the next free slot in the queue */
	endpoint->next_free_unexp_eventq_offset += OMX_EVENTQ_ENTRY_SIZE;
	if (unlikely(endpoint->next_free_unexp_eventq_offset >= OMX_UNEXP_EVENTQ_SIZE))
		endpoint->next_free_unexp_eventq_offset = 0;

	/* find and update the next reserved slot in the queue */
	slot = endpoint->unexp_eventq + endpoint->next_reserved_unexp_eventq_offset;
	endpoint->next_reserved_unexp_eventq_offset += OMX_EVENTQ_ENTRY_SIZE;
	if (unlikely(endpoint->next_reserved_unexp_eventq_offset >= OMX_UNEXP_EVENTQ_SIZE))
		endpoint->next_reserved_unexp_eventq_offset = 0;

	spin_unlock(&endpoint->event_lock);

	/* store the event */
	memcpy(slot, event, length);
	wmb();
	((struct omx_evt_generic *) slot)->type = type;

	return 0;
}

/********************************************
 * Report an unexpected event to users-space
 * with a recvq slot needed
 */

/*
 * The recvq accounting is trivial since there are as many recvq slot
 * than unexp event slot, the latter are accounted, and we allocate only
 * one recvq slot per prepare()/commit() functions below (and no slot
 * in notify() above).
 */

/* Reserve one more slot and returns the corresponding recvq slot to the caller */
int
omx_prepare_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint,
					  char ** recvq_slot_p)
{
	union omx_evt *slot;

	spin_lock(&endpoint->event_lock);

	/* check that there's a slot available and reserve it */
	slot = endpoint->unexp_eventq + endpoint->next_free_unexp_eventq_offset;
	if (unlikely(slot->generic.type != OMX_EVT_NONE)) {
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		spin_unlock(&endpoint->event_lock);
		return -EBUSY;
	}

	/* update the next free slot in the queue */
	endpoint->next_free_unexp_eventq_offset += OMX_EVENTQ_ENTRY_SIZE;
	if (unlikely(endpoint->next_free_unexp_eventq_offset >= OMX_UNEXP_EVENTQ_SIZE))
		endpoint->next_free_unexp_eventq_offset = 0;

	/* take the next recvq slot and return it now */
	endpoint->next_recvq_slot = endpoint->recvq
		+ (((void *) slot - endpoint->unexp_eventq)
		   << (OMX_RECVQ_ENTRY_SHIFT - OMX_EVENTQ_ENTRY_SHIFT));
	*recvq_slot_p = endpoint->next_recvq_slot;

	spin_unlock(&endpoint->event_lock);
	return 0;
}

/*
 * Store the event in the next reserved slot
 * (not always the one reserved during omx_commit_notify_unexp_event()
 *  since prepare/commit calls could have been overlapped).
 */
void
omx_commit_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint,
					 uint8_t type, void *event, int length)
{
	union omx_evt *slot;

	spin_lock(&endpoint->event_lock);

	/* the caller should have called prepare() earlier */
	BUG_ON(endpoint->next_reserved_unexp_eventq_offset == endpoint->next_free_unexp_eventq_offset);

	/* update the next reserved slot in the queue */
	slot = endpoint->unexp_eventq + endpoint->next_reserved_unexp_eventq_offset;
	endpoint->next_reserved_unexp_eventq_offset += OMX_EVENTQ_ENTRY_SIZE;
	if (unlikely(endpoint->next_reserved_unexp_eventq_offset >= OMX_UNEXP_EVENTQ_SIZE))
		endpoint->next_reserved_unexp_eventq_offset = 0;

	spin_unlock(&endpoint->event_lock);

	/* store the event */
	memcpy(slot, event, length);
	wmb();
	((struct omx_evt_generic *) slot)->type = type;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
