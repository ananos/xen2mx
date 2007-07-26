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

#ifndef __omx_hal_h__
#define __omx_hal_h__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>

#include "omx_checks.h"

/* FIXME: useless now */
#ifdef OMX_SKB_LINEARIZE_HAS_GFP
#define omx_skb_linearize(skb) skb_linearize(skb, GFP_ATOMIC)
#else
#define omx_skb_linearize skb_linearize
#endif

#ifdef OMX_HAVE_NETDEV_ALLOC_SKB
#define omx_netdev_alloc_skb netdev_alloc_skb
#else /* OMX_HAVE_NETDEV_ALLOC_SKB */
static inline struct sk_buff *
omx_netdev_alloc_skb(struct net_device * dev, unsigned length)
{
	struct sk_buff * skb = dev_alloc_skb(length);
	if (likely(skb))
		skb->dev = dev;
	return skb;
}
#endif /* OMX_HAVE_NETDEV_ALLOC_SKB */

#ifdef OMX_HAVE_REMAP_VMALLOC_RANGE
#define omx_vmalloc_user vmalloc_user
#define omx_remap_vmalloc_range remap_vmalloc_range
#else /* OMX_HAVE_REMAP_VMALLOC_RANGE */
static inline void *
omx_vmalloc_user(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL);
}

static inline int
omx_remap_vmalloc_range(struct vm_area_struct *vma, void *addr, unsigned long pgoff)
{
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	int ret;

	addr += pgoff << PAGE_SHIFT;
	do {
		struct page *page = vmalloc_to_page(addr);
		ret = vm_insert_page(vma, uaddr, page);
		if (ret)
			return ret;

		uaddr += PAGE_SIZE;
		addr += PAGE_SIZE;
		usize -= PAGE_SIZE;
	} while (usize > 0);

	/* Prevent "things" like memory migration? VM_flags need a cleanup... */
	vma->vm_flags |= VM_RESERVED;

	return ret;
}
#endif /* OMX_HAVE_REMAP_VMALLOC_RANGE */

#ifdef OMX_HAVE_FOR_EACH_NETDEV
#define omx_for_each_netdev(_ifp) for_each_netdev(_ifp)
#else /* OMX_HAVE_FOR_EACH_NETDEV */
#define omx_for_each_netdev(_ifp) for ((_ifp) = dev_base; (_ifp) != NULL; (_ifp) = (_ifp)->next)
#endif /* OMX_HAVE_FOR_EACH_NETDEV */

#ifdef OMX_HAVE_SKB_HEADERS
#define omx_skb_reset_mac_header skb_reset_mac_header
#define omx_skb_reset_network_header skb_reset_network_header
#define omx_hdr(skb) ((struct omx_hdr *) skb_mac_header(skb))
#else /* OMX_HAVE_SKB_HEADERS */
#define omx_skb_reset_mac_header(skb) skb->mac.raw = skb->data
#define omx_skb_reset_network_header(skb) skb->nh.raw = skb->mac.raw
#define omx_hdr(skb) ((struct omx_hdr *) skb->mac.raw)
#endif /* OMX_HAVE_SKB_HEADERS */

#ifdef OMX_SKB_PAD_RETURNS_NEW_SKB
#define omx_skb_pad(skb, pad) ({ skb = skb_pad(skb, pad); skb == NULL ? -ENOMEM : 0; })
#else
#define omx_skb_pad skb_pad
#endif

#ifdef OMX_HAVE_UTS_NAMESPACE
/* uts namespace introduced in 2.6.19 */
#define omx_current_utsname current->nsproxy->uts_ns->name
#else
#define omx_current_utsname system_utsname
#endif

#endif /* __omx_hal_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
