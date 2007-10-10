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

void
omx_endpoint_queues_init(struct omx_endpoint *endpoint)
{
	union omx_evt * evt;

	for(evt = endpoint->exp_eventq;
	    (void *) evt < endpoint->exp_eventq + OMX_EXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = OMX_EVT_NONE;
	endpoint->next_exp_eventq_slot = endpoint->exp_eventq;

	for(evt = endpoint->unexp_eventq;
	    (void *) evt < endpoint->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = OMX_EVT_NONE;
	endpoint->next_unexp_eventq_slot = endpoint->unexp_eventq;

	endpoint->next_recvq_slot = endpoint->recvq;
}

union omx_evt *
omx_find_next_exp_eventq_slot(struct omx_endpoint *endpoint)
{
	/* FIXME: need locking */
	union omx_evt *slot = endpoint->next_exp_eventq_slot;
	if (unlikely(slot->generic.type != OMX_EVT_NONE)) {
		dprintk(EVENT,
			"Open-MX: Expected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		return NULL;
	}

	endpoint->next_exp_eventq_slot = slot + 1;
	if (unlikely((void *) endpoint->next_exp_eventq_slot
		     >= endpoint->exp_eventq + OMX_EXP_EVENTQ_SIZE))
		endpoint->next_exp_eventq_slot = endpoint->exp_eventq;

	return slot;
}

union omx_evt *
omx_find_next_unexp_eventq_slot(struct omx_endpoint *endpoint,
				char ** recvq_slot_p)
{
	/* FIXME: need locking */
	union omx_evt *slot = endpoint->next_unexp_eventq_slot;
	if (unlikely(slot->generic.type != OMX_EVT_NONE)) {
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		return NULL;
	}

	endpoint->next_unexp_eventq_slot = slot + 1;
	if (unlikely((void *) endpoint->next_unexp_eventq_slot
		     >= endpoint->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE))
		endpoint->next_unexp_eventq_slot = endpoint->unexp_eventq;

	/* recvq slot is at same index for now */
	endpoint->next_recvq_slot = endpoint->recvq
		+ (((void *) slot - endpoint->unexp_eventq)
		   << (OMX_RECVQ_ENTRY_SHIFT - OMX_EVENTQ_ENTRY_SHIFT));

	if (recvq_slot_p)
		*recvq_slot_p = endpoint->next_recvq_slot;

	return slot;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
