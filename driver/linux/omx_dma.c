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
#include <linux/dmaengine.h>
#include <linux/netdevice.h>
#include <net/netdma.h>
#include <linux/rcupdate.h>

#include "omx_common.h"
#include "omx_endpoint.h"
#include "omx_dma.h"

int
omx_dma_skb_copy_datagram_to_pages(void *handle,
				   struct sk_buff *skb, int offset,
				   struct page **pages, int pgoff,
				   size_t len)
{
	struct dma_chan *chan = handle;
	int start = skb_headlen(skb);
	int i, copy;
	dma_cookie_t cookie = 0;

	/* Copy header. */
	copy = start - offset;
	while (copy > 0) {
		int chunk;

		chunk = min_t(int, copy, len);
		chunk = min_t(int, copy, PAGE_SIZE - pgoff);

		cookie = dma_async_memcpy_buf_to_pg(chan,
						    *pages, pgoff,
						    skb->data + offset,
						    chunk);
		if (cookie < 0)
			goto fault;

		len -= chunk;
		if (len == 0)
			goto end;

		copy -= chunk;

		offset += chunk;
		pgoff += chunk;
		if (pgoff == PAGE_SIZE)
			pages++;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		struct page *page = frag->page;

		BUG_ON(start > offset + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		copy = end - offset;
		while (copy > 0) {
			int chunk;

			chunk = min_t(int, copy, len);
			chunk = min_t(int, copy, PAGE_SIZE - pgoff);

			cookie = dma_async_memcpy_pg_to_pg(chan,
							   *pages, pgoff,
							   page, frag->page_offset + offset - start,
							   chunk);
			if (cookie < 0)
				goto fault;

			len -= chunk;
			if (len == 0)
				goto end;

			copy -= chunk;

			offset += chunk;
			pgoff += chunk;
			if (pgoff == PAGE_SIZE)
				pages++;
		}
		start = end;
	}

	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;

		for (; list; list = list->next) {
			int end;

			BUG_ON(start > offset + len);

			end = start + list->len;
			copy = end - offset;
			if (copy > 0) {
				if (copy > len)
					copy = len;

				cookie = omx_dma_skb_copy_datagram_to_pages(chan, list, offset - start, pages, pgoff, copy);
				if (cookie < 0)
					goto fault;

				len -= copy;
				if (len == 0)
					goto end;

				offset += copy;
				pgoff += copy;
				pages += (pgoff >> PAGE_SHIFT);
				pgoff &= ~PAGE_MASK;
			}
			start = end;
		}
	}

 end:
	if (!len) {
		skb->dma_cookie = cookie;
		return cookie;
	}

 fault:
	return -EFAULT;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
