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

#include "omx_common.h"

/* DMA Engine must be built in the kernel and as a module,
 * and it must provide shareable channels.
 */
#if (defined CONFIG_DMA_ENGINE || defined CONFIG_DMA_ENGINE_MODULE)
#ifdef OMX_HAVE_SHAREABLE_DMA_CHANNELS
#define OMX_MAY_USE_DMA_ENGINE 1
#endif
#endif

#ifdef OMX_MAY_USE_DMA_ENGINE

#include <linux/dmaengine.h>

static spinlock_t omx_dma_lock;

static enum dma_state_client
omx_dma_event_callback(struct dma_client *client, struct dma_chan *chan,
		       enum dma_state state)
{
	enum dma_state_client ack = DMA_DUP; /* default: take no action */

	spin_lock(&omx_dma_lock);

	switch (state) {
	case DMA_RESOURCE_AVAILABLE:
		printk(KERN_INFO "Open-MX: DMA channel available.\n");
		break;
	case DMA_RESOURCE_REMOVED:
		printk(KERN_INFO "Open-MX: DMA channel removed.\n");
		break;
	default:
		break;
	}

	spin_unlock(&omx_dma_lock);

	return ack;
}

static struct dma_client omx_dma_client = {
	.event_callback = omx_dma_event_callback,
};

#endif /* OMX_MAY_USE_DMA_ENGINE */

int
omx_dma_init(void)
{
#ifdef OMX_MAY_USE_DMA_ENGINE
	spin_lock_init(&omx_dma_lock);
	dma_cap_set(DMA_MEMCPY, omx_dma_client.cap_mask);
	dma_async_client_register(&omx_dma_client);
	dma_async_client_chan_request(&omx_dma_client);
	printk(KERN_INFO "Open-MX: Registered DMA event callback\n");
#elif (defined CONFIG_DMA_ENGINE || defined CONFIG_DMA_ENGINE_MODULE)
	printk(KERN_INFO "Open-MX: Not using DMA engine channels since they are not shareable\n");
#endif
	return 0;
}

void
omx_dma_exit(void)
{
#ifdef OMX_MAY_USE_DMA_ENGINE
	dma_async_client_unregister(&omx_dma_client);
#endif
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
