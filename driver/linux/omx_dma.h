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

#ifndef __omx_dma_h__
#define __omx_dma_h__

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/moduleparam.h>

#include "omx_hal.h"

#ifdef OMX_HAVE_DMA_ENGINE

extern int omx_dmaengine;
extern int omx_dma_async_frag_min;
extern int omx_dma_async_min;
extern int omx_dma_sync_min;

extern int omx_dma_init(void);
extern void omx_dma_exit(void);

extern int omx_dma_skb_copy_datagram_to_pages(struct dma_chan *chan, dma_cookie_t *cookiep, const struct sk_buff *skb, int offset, struct page * const *pages, int pgoff, size_t len);
extern int omx_dma_skb_copy_datagram_to_user_region(struct dma_chan *chan, dma_cookie_t *cookiep, const struct sk_buff *skb, struct omx_user_region *region, uint32_t regoff, size_t len);

#else /* OMX_HAVE_DMA_ENGINE */

static inline int omx_dma_init(void) { return 0; }
static inline void omx_dma_exit(void) { /* nothing */ }

#endif /* OMX_HAVE_DMA_ENGINE */

#endif /* __omx_dma_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
