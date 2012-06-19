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

#ifndef __omx_xenback_reg_h__
#define __omx_xenback_reg_h__

#include <stdarg.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/e820.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/xenbus.h>
#include <xen/page.h>

#include "omx_xen.h"
#include "omx_xenback.h"
#include "omx_xenback_event.h"

int omx_xen_create_user_region(omx_xenif_t * omx_xenif, uint32_t id,
			       uint64_t vaddr, uint32_t nr_segments,
			       uint32_t nr_pages, uint32_t nr_grefs,
			       uint8_t eid);
int omx_xen_destroy_user_region(omx_xenif_t * omx_xenif, uint32_t id,
				uint32_t seqnum, uint8_t eid);

int omx_xen_register_user_segment(omx_xenif_t * omx_xenif,
				  struct omx_ring_msg_register_user_segment *req);

int omx_xen_deregister_user_segment(omx_xenif_t * omx_xenif, uint32_t id,
				    uint32_t sid, uint8_t eid);
void omx_xen_user_region_destroy_segments(struct omx_xen_user_region *region,
					  struct omx_endpoint *endpoint);

int omx_xen_map_page(struct backend_info *be, uint32_t gref, void **vaddr,
		     uint32_t * handle, struct page **page,
		     struct omx_xen_page_cookie **cookie);
int omx_xen_unmap_page(uint32_t handle, struct page *page);

void omx_xen_user_region_release(struct omx_xen_user_region *region);
struct omx_xen_user_region *omx_xen_user_region_acquire(const struct
							omx_endpoint *endpoint,
							uint32_t rdma_id);

int omx_xen_user_region_offset_cache_init(struct omx_xen_user_region *region,
					  struct omx_user_region_offset_cache
					  *cache, unsigned long offset,
					  unsigned long length);

int
omx_user_region_offset_cache_contig_append_callback(struct
						    omx_user_region_offset_cache
						    *cache, struct sk_buff *skb,
						    unsigned long length);
int omx_user_region_offset_cache_vect_append_callback(struct
						      omx_user_region_offset_cache
						      *cache,
						      struct sk_buff *skb,
						      unsigned long length);
void omx_user_region_offset_cache_contig_copy_callback(struct
						       omx_user_region_offset_cache
						       *cache, void *buffer,
						       unsigned long length);
void omx_user_region_offset_cache_vect_copy_callback(struct
						     omx_user_region_offset_cache
						     *cache, void *buffer,
						     unsigned long length);
int omx_user_region_offset_cache_dma_contig_memcpy_from_buf_callback(struct
								     omx_user_region_offset_cache
								     *cache,
								     struct
								     dma_chan
								     *chan,
								     dma_cookie_t
								     * cookiep,
								     const void
								     *buffer,
								     unsigned
								     long
								     length);
int omx_user_region_offset_cache_dma_vect_memcpy_from_buf_callback(struct
								   omx_user_region_offset_cache
								   *cache,
								   struct
								   dma_chan
								   *chan,
								   dma_cookie_t
								   * cookiep,
								   const void
								   *buffer,
								   unsigned long
								   length);
int omx_user_region_offset_cache_dma_contig_memcpy_from_pg_callback(struct
								    omx_user_region_offset_cache
								    *cache,
								    struct
								    dma_chan
								    *chan,
								    dma_cookie_t
								    * cookiep,
								    struct page
								    *skbpage,
								    int
								    skbpgoff,
								    unsigned
								    long
								    length);
int omx_user_region_offset_cache_dma_vect_memcpy_from_pg_callback(struct
								  omx_user_region_offset_cache
								  *cache,
								  struct
								  dma_chan
								  *chan,
								  dma_cookie_t *
								  cookiep,
								  struct page
								  *skbpage,
								  int skbpgoff,
								  unsigned long
								  length);

#endif				/* __omx_xenback_reg_h__ */
/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
