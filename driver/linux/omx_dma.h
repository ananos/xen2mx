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

#ifndef __omx_dma_h__
#define __omx_dma_h__

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#ifdef CONFIG_NET_DMA
#include <net/netdma.h>
#endif

/* enable/disable DMA engine usage at runtime */
extern int omx_dmaengine;
/* threshold to offload copy */
extern int omx_dma_min;

/* initialization and termination of the dma manager */
static inline int omx_dma_init(void) { return 0; }
static inline void omx_dma_exit(void) { /* nothing */ }

/* dma channel manipulation, if available */
static inline void *
omx_dma_get_handle(struct omx_endpoint *endpoint)
{
#ifdef CONFIG_NET_DMA
	return get_softnet_dma();
#else
	return NULL;
#endif
}

static inline void
omx_dma_put_handle(struct omx_endpoint *endpoint, void *handle)
{
#ifdef CONFIG_NET_DMA
	dma_chan_put((struct dma_chan *) handle);
#endif
}

/* basic dma descriptor management, if available */
static inline void
omx_dma_handle_push(void *handle)
{
#ifdef CONFIG_NET_DMA
	dma_async_memcpy_issue_pending((struct dma_chan *) handle);
#endif
}

static inline void
omx_dma_handle_wait(void *handle, struct sk_buff *skb)
{
#ifdef CONFIG_NET_DMA
	while (dma_async_memcpy_complete((struct dma_chan *) handle,
					 skb->dma_cookie, NULL, NULL) == DMA_IN_PROGRESS);
#endif
}

#ifdef CONFIG_NET_DMA

extern int omx_dma_skb_copy_datagram_to_pages(void *handle, struct sk_buff *skb, int offset, struct page **pages, int pgoff, size_t len);

#else

static inline int omx_dma_skb_copy_datagram_to_pages(void *handle, struct sk_buff *skb, int offset, struct page **pages, int pgoff, size_t len) { return -ENOSYS; }

#endif

#endif /* __omx_dma_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
