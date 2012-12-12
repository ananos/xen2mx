/*
 * Open-MX
 * Copyright © inria 2007-2009 (see AUTHORS file)
 * Copyright © Anastassios Nanos 2012
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
#include <linux/rcupdate.h>

#include "omx_common.h"
#include "omx_endpoint.h"
#include "omx_reg.h"
#include "omx_xenback_reg.h"
#include "omx_dma.h"

#ifdef OMX_HAVE_DMA_ENGINE

int
omx_xen_dma_skb_copy_datagram_to_pages(struct dma_chan *chan, dma_cookie_t *cookiep,
				   const struct sk_buff *skb, int offset,
				   struct page * const *pages, int pgoff,
				   size_t len)
{
	int start = skb_headlen(skb);
	int i, copy;
	dma_cookie_t cookie = 0;
	int err;

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
			goto end;

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
		struct page *page = skb_frag_page(frag);

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
				goto end;

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

				err = omx_xen_dma_skb_copy_datagram_to_pages(chan, &cookie, list, offset - start, pages, pgoff, copy);
				if (err > 0) {
					len -= (copy - err);
					goto end;
				}

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
	*cookiep = cookie;
	return len;
}

static int
omx__xen_dma_skb_copy_datagram_to_user_region(struct omx_user_region_offset_cache *regcache,
					  struct dma_chan *chan, dma_cookie_t *cookiep,
					  const struct sk_buff *skb, int skboff,
					  size_t len)
{
	int start = skb_headlen(skb);
	int i, copy = start - skboff;
	dma_cookie_t cookie = 0;
	int err;

	/* Copy header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		err = regcache->dma_memcpy_from_buf(regcache, chan, &cookie, skb->data + skboff, copy);
		if (err > 0) {
			len -= (copy - err);
			goto end;
		}
		len -= copy;
		if (len == 0)
			goto end;
		skboff += copy;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		BUG_ON(start > skboff + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		copy = end - skboff;
		if (copy > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			struct page *page = skb_frag_page(frag);

			if (copy > len)
				copy = len;

			err = regcache->dma_memcpy_from_pg(regcache, chan, &cookie, page, frag->page_offset + skboff - start, copy);
			if (err > 0) {
				len -= (copy - err);
				goto end;
			}
			len -= copy;
			if (len == 0)
				goto end;
			skboff += copy;
		}
		start = end;
	}

	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;

		for (; list; list = list->next) {
			int end;

			BUG_ON(start > skboff + len);

			end = start + list->len;
			copy = end - skboff;
			if (copy > 0) {
				if (copy > len)
					copy = len;
				err = omx__xen_dma_skb_copy_datagram_to_user_region(regcache, chan, &cookie, list, skboff - start, copy);
				if (err > 0) {
					len -= (copy - err);
					goto end;
				}
				len -= copy;
				if (len == 0)
					goto end;
				skboff += copy;
			}
			start = end;
		}
	}

end:
	*cookiep = cookie;
	return len;
}

int
omx_xen_dma_skb_copy_datagram_to_user_region(struct dma_chan *chan, dma_cookie_t *cookiep,
					 const struct sk_buff *skb,
					 struct omx_xen_user_region *xregion, uint32_t regoff,
					 size_t len)
{
	struct omx_user_region_offset_cache regcache;
	unsigned long skb_offset = sizeof(struct omx_pkt_head) + sizeof(struct omx_pkt_pull_reply);
	int err;

	err = omx_xen_user_region_offset_cache_init(xregion, &regcache, regoff, len);
	if (err < 0)
		return err;

	return omx__xen_dma_skb_copy_datagram_to_user_region(&regcache, chan, cookiep, skb, skb_offset, len);
}

#endif /* OMX_HAVE_DMA_ENGINE */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
