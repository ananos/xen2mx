#ifndef __mpoe_hal_h__
#define __mpoe_hal_h__

#include "mpoe_checks.h"

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>

/* FIXME: useless now */
#ifdef MPOE_SKB_LINEARIZE_HAS_GFP
#define mpoe_skb_linearize(skb) skb_linearize(skb, GFP_ATOMIC)
#else
#define mpoe_skb_linearize skb_linearize
#endif

#ifdef MPOE_HAVE_NETDEV_ALLOC_SKB
#define mpoe_netdev_alloc_skb netdev_alloc_skb
#else /* MPOE_HAVE_NETDEV_ALLOC_SKB */
static inline struct sk_buff *
mpoe_netdev_alloc_skb(struct net_device * dev, unsigned length)
{
	struct sk_buff * skb = dev_alloc_skb(length);
	if (likely(skb))
		skb->dev = dev;
	return skb;
}
#endif /* MPOE_HAVE_NETDEV_ALLOC_SKB */

#ifdef MPOE_HAVE_REMAP_VMALLOC_RANGE
#define mpoe_vmalloc_user vmalloc_user
#define mpoe_remap_vmalloc_range remap_vmalloc_range
#else /* MPOE_HAVE_REMAP_VMALLOC_RANGE */
static inline void *
mpoe_vmalloc_user(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL);
}

static inline int
mpoe_remap_vmalloc_range(struct vm_area_struct *vma, void *addr, unsigned long pgoff)
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
#endif /* MPOE_HAVE_REMAP_VMALLOC_RANGE */

#ifdef MPOE_HAVE_FOR_EACH_NETDEV
#define mpoe_for_each_netdev(_ifp) for_each_netdev(_ifp)
#else /* MPOE_HAVE_FOR_EACH_NETDEV */
#define mpoe_for_each_netdev(_ifp) for ((_ifp) = dev_base; (_ifp) != NULL; (_ifp) = (_ifp)->next)
#endif /* MPOE_HAVE_FOR_EACH_NETDEV */

#ifdef MPOE_HAVE_SKB_HEADERS
#define mpoe_skb_reset_mac_header skb_reset_mac_header
#define mpoe_skb_reset_network_header skb_reset_network_header
#define mpoe_hdr(skb) ((struct mpoe_hdr *) skb_mac_header(skb))
#else /* MPOE_HAVE_SKB_HEADERS */
#define mpoe_skb_reset_mac_header(skb) skb->mac.raw = skb->data
#define mpoe_skb_reset_network_header(skb) skb->nh.raw = skb->mac.raw
#define mpoe_hdr(skb) ((struct mpoe_hdr *) skb->mac.raw)
#endif /* MPOE_HAVE_SKB_HEADERS */

#ifdef MPOE_SKB_PAD_RETURNS_NEW_SKB
#define mpoe_skb_pad(skb, pad) ({ skb = skb_pad(skb, pad); skb == NULL ? -ENOMEM : 0; })
#else
#define mpoe_skb_pad skb_pad
#endif

#ifdef MPOE_HAVE_UTS_NAMESPACE
/* uts namespace introduced in 2.6.19 */
#define mpoe_current_utsname current->nsproxy->uts_ns->name
#else
#define mpoe_current_utsname system_utsname
#endif

#endif /* __mpoe_hal_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
