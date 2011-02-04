/*
 * Open-MX
 * Copyright © INRIA 2007-2011 (see AUTHORS file)
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
#include <linux/mm.h>

/* __maybe_unused appeared in 2.6.22 */
#ifndef __maybe_unused
#define __maybe_unused /* not implemented */
#endif

/* __pure appeared in 2.6.21 */
#ifndef __pure
/* __attribute_pure__ disappeared in 2.6.23 */
#ifdef __attribute_pure__
#define __pure __attribute_pure__
#else
#define __pure /* not implemented */
#endif
#endif

#ifdef OMX_HAVE_VMALLOC_USER
#define omx_vmalloc_user vmalloc_user
#else /* !OMX_HAVE_VMALLOC_USER */
#include <asm/pgtable.h>
static inline void *
omx_vmalloc_user(unsigned long size)
{
	/* don't pass __GFP_ZERO since cache_grow() would BUG() in <=2.6.18 */
	void * buf = __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
	if (buf) {
		/*
		 * We cannot set VM_USERMAP since __find_vm_area() is not exported.
		 * But remap_vmalloc_range() requires it, see below
		 */

		/* memset since we didn't pass __GFP_ZERO above */
		memset(buf, 0, size);
	}

	return buf;
}
#endif /* !OMX_HAVE_VMALLOC_USER */

#if (defined OMX_HAVE_REMAP_VMALLOC_RANGE) && !(defined OMX_HAVE_VMALLOC_USER)
/*
 * Do not use the official remap_vmalloc_range() since it requires VM_USERMAP
 * in the area flags but our omx_vmalloc_user() above could not set it.
 */
#undef OMX_HAVE_REMAP_VMALLOC_RANGE
#endif

#ifdef OMX_HAVE_REMAP_VMALLOC_RANGE
#define omx_remap_vmalloc_range remap_vmalloc_range
#else /* !OMX_HAVE_REMAP_VMALLOC_RANGE */
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
#endif /* !OMX_HAVE_REMAP_VMALLOC_RANGE */

#ifdef OMX_HAVE_FOR_EACH_NETDEV
#define omx_for_each_netdev(_ifp) for_each_netdev(&init_net, _ifp)
#elif defined OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS
#define omx_for_each_netdev(_ifp) for_each_netdev(_ifp)
#else /* !OMX_HAVE_FOR_EACH_NETDEV && !OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS */
#define omx_for_each_netdev(_ifp) for ((_ifp) = dev_base; (_ifp) != NULL; (_ifp) = (_ifp)->next)
#endif /* !OMX_HAVE_FOR_EACH_NETDEV && !OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS */

#ifdef OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS
#define omx_dev_get_by_name dev_get_by_name
#else /* !OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS */
#define omx_dev_get_by_name(name) dev_get_by_name(&init_net, name)
#endif /* !OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS */

#ifdef OMX_HAVE_SKB_HEADERS
#define omx_skb_reset_mac_header skb_reset_mac_header
#define omx_skb_reset_network_header skb_reset_network_header
#define omx_skb_mac_header(skb) ((struct omx_hdr *) skb_mac_header(skb))
#else /* !OMX_HAVE_SKB_HEADERS */
#define omx_skb_reset_mac_header(skb) skb->mac.raw = skb->data
#define omx_skb_reset_network_header(skb) skb->nh.raw = skb->mac.raw
#define omx_skb_mac_header(skb) ((struct omx_hdr *) skb->mac.raw)
#endif /* !OMX_HAVE_SKB_HEADERS */

#ifdef OMX_HAVE_TASK_STRUCT_NSPROXY
/* task_struct ns_proxy introduced in 2.6.19 */
#define omx_current_utsname current->nsproxy->uts_ns->name
#else
#define omx_current_utsname system_utsname
#endif

#ifndef OMX_HAVE_MUTEX
/* mutex introduced in 2.6.16 */
#include <asm/semaphore.h>
#define mutex semaphore
#define mutex_init(m) sema_init(m, 1)
#define mutex_lock(m) down(m)
#define mutex_unlock(m) up(m)
#endif

/* list_first_entry appeared in 2.6.22 */
#ifndef list_first_entry
#define list_first_entry(ptr, type, member) \
list_entry((ptr)->next, type, member)
#endif

/* net_device switch from class_device to device in 2.6.21 */
#ifdef OMX_HAVE_NETDEVICE_CLASS_DEVICE
#define omx_ifp_to_dev(ifp) (ifp)->class_dev.dev
#else
#define omx_ifp_to_dev(ifp) (ifp)->dev.parent
#endif

/* dev_to_node appeared in 2.6.20 */
#ifdef OMX_HAVE_DEV_TO_NODE
static inline __pure int
omx_ifp_node(struct net_device *ifp)
{
  struct device *dev = omx_ifp_to_dev(ifp);
  return dev ? dev_to_node(dev) : -1;
}
#else
#define omx_ifp_node(ifp) -1
#endif

/* work_struct switch to container_of in 2.6.20 */
#ifdef OMX_HAVE_WORK_STRUCT_DATA
#define OMX_INIT_WORK(_work, _func, _data) INIT_WORK(_work, _func, _data)
#define OMX_WORK_STRUCT_DATA(_data, _type, _field) (_data)
typedef void * omx_work_struct_data_t;
#else
#define OMX_INIT_WORK(_work, _func, _data) INIT_WORK(_work, _func)
#define OMX_WORK_STRUCT_DATA(_data, _type, _field) container_of(_data, _type, _field)
typedef struct work_struct * omx_work_struct_data_t;
#endif

/* 64bits jiffies comparison routines appeared in 2.6.19 */
#include <linux/jiffies.h>
#ifndef time_after64
#define time_after64(a,b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) &&	\
	((__s64)(b) - (__s64)(a) < 0))
#define time_before64(a,b)	time_after64(b,a)

#define time_after_eq64(a,b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) &&	\
	((__s64)(a) - (__s64)(b) >= 0))
#define time_before_eq64(a,b) 	time_after_eq64(b,a)
#endif

#ifdef OMX_HAVE_OLD_DMA_ENGINE_API

/* kernel <= 2.6.28 with DMA engine support through NET_DMA */
#ifdef CONFIG_NET_DMA
#define OMX_HAVE_DMA_ENGINE 1
#include <net/netdma.h>
#include <linux/netdevice.h>
#define omx_dmaengine_get() do { /* do nothing */ } while (0)
#define omx_dmaengine_put() do { /* do nothing */ } while (0)
#define omx_dma_chan_avail() __get_cpu_var(softnet_data).net_dma
#define omx_dma_chan_get() get_softnet_dma()
#define omx_dma_chan_put(chan) dma_chan_put(chan)
#else
#define OMX_DMA_ENGINE_CONFIG_STR "CONFIG_NET_DMA"
#endif

#elif defined OMX_HAVE_DMA_ENGINE_API

/* kernel >= 2.6.29 with nice DMA engine suport */
#ifdef CONFIG_DMA_ENGINE
#define OMX_HAVE_DMA_ENGINE 1
#include <linux/dmaengine.h>
#define omx_dmaengine_get() dmaengine_get()
#define omx_dmaengine_put() dmaengine_put()
#define omx_dma_chan_avail() dma_find_channel(DMA_MEMCPY)
#define omx_dma_chan_get() dma_find_channel(DMA_MEMCPY)
#define omx_dma_chan_put(chan) do { /* do nothing */ } while (0)
#else
#define OMX_DMA_ENGINE_CONFIG_STR "CONFIG_DMA_ENGINE"
#endif

#else /* !OMX_HAVE_{OLD_,}DMA_ENGINE_API */

/* kernel <= 2.6.17 with no DMA engine at all */
#define OMX_DMA_ENGINE_CONFIG_STR "CONFIG_DMA_ENGINE"

#endif /* !OMX_HAVE_{OLD_,}DMA_ENGINE_API */

#ifdef OMX_HAVE_DEV_NAME
#define omx_dev_name dev_name
#else
#define omx_dev_name(dev) ((dev)->bus_id)
#endif

#ifdef OMX_HAVE_MOD_TIMER_PENDING
#define omx_mod_timer_pending mod_timer_pending
#else
#define omx_mod_timer_pending __mod_timer
#endif

/* rcu helpers added in 2.6.34 */
#ifndef rcu_dereference_protected
#define rcu_dereference_protected(x, c) (x)
#endif
#ifndef rcu_access_pointer
#define rcu_access_pointer(x) (x)
#endif

/* rcu helper added in 2.6.37 */
#ifndef RCU_INIT_POINTER
#define RCU_INIT_POINTER(p, v) p = (typeof(*v) __force __rcu *)(v)
#endif

/* sparse rcu pointer dereferencing check added in 2.6.37 */
#ifndef __rcu
#define __rcu
#endif

#ifdef OMX_HAVE_GET_USER_PAGES_FAST
/* get_user_pages_fast doesn't like large regions, so split it into batches */
static inline int
omx_get_user_pages_fast(unsigned long start, int nr_pages, int write, struct page **pages)
{
	int ret;
	int done;

	done = 0;
	while (nr_pages) {
#define OMX_GET_USER_PAGES_FAST_BATCH 32
		int chunk = nr_pages > OMX_GET_USER_PAGES_FAST_BATCH ? OMX_GET_USER_PAGES_FAST_BATCH : nr_pages;
		ret = get_user_pages_fast(start, chunk, write, pages);
		if (ret != chunk) {
			ret = done + (ret < 0 ? 0 : ret);
			goto out;
		}
		pages += chunk;
		start += chunk << PAGE_SHIFT;
		done += chunk;
		nr_pages -= chunk;
	}
	ret = done;

 out:
	return ret;
}
#else /* !OMX_HAVE_GET_USER_PAGES_FAST */
/* revert to the old locked get_user_pages */
static inline int
omx_get_user_pages_fast(unsigned long start, int nr_pages, int write, struct page **pages)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_read(&mm->mmap_sem);
	ret = get_user_pages(current, mm, start, nr_pages, write, 0, pages, NULL);
	up_read(&mm->mmap_sem);

	return ret;
}
#endif /* !OMX_HAVE_GET_USER_PAGES_FAST */

#endif /* __omx_hal_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
