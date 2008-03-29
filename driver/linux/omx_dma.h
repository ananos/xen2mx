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

/* enable/disable DMA engine usage at runtime */
extern int omx_dmaengine;

/* initialization and termination of the dma manager */
#ifdef CONFIG_DMA_ENGINE
extern int omx_dma_init(void);
extern void omx_dma_exit(void);
#else
static inline int omx_dma_init(void) { return 0; }
static inline void omx_dma_exit(void) { /* nothing */ }
#endif

/* dma channel manipulation, if available */
#ifdef CONFIG_DMA_ENGINE

extern void * omx_dma_get_handle(struct omx_endpoint *endpoint);
extern void omx_dma_put_handle(struct omx_endpoint *endpoint, void *handle);

extern void omx_dma_handle_push(void *handle);
extern void omx_dma_handle_wait(void *handle, struct sk_buff *skb);

extern int omx_dma_skb_copy_datagram_to_pages(void *handle, struct sk_buff *skb, int offset, struct page **pages, int pgoff, size_t len);

#else

static inline void * omx_dma_get_handle(struct omx_endpoint *endpoint) { return NULL; }
static inline void omx_dma_put_handle(struct omx_endpoint *endpoint, void *handle) { /* nothing */ }

static inline void omx_dma_handle_push(void *handle) { /* nothing */ }
static inline void omx_dma_handle_wait(void *handle, struct sk_buff *skb) { /* nothing */ }

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
