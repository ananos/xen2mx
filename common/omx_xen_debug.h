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

#ifndef __omx_xen_debug_h__
#define __omx_xen_debug_h__

#ifdef EXTRA_DEBUG_OMX
#define ddprintk(args...) printk(args)
#else
#define ddprintk(args...)
#endif /* OMX_DEBUG */

#define dprintk_inf(args...) printk(KERN_INFO "Xen-OMX: " args)
#define dprintk_deb(args...) ddprintk(KERN_ERR "Xen-OMX: " args)
#define dprintk_warn(args...) dprintk(KERN_WARNING "Xen-OMX: " args)

#define dprintk_in()  dprintk_deb("Into function %s\n", __func__)
#define dprintk_out() dprintk_deb("Out of function %s\n", __func__)

#define printk_err(args...) printk(KERN_ERR "OMX ERROR: " args)
#define printk_warn(args...) printk(KERN_WARNING "OMX WARNING: " args)
#define printk_debug(args...) printk(KERN_DEBUG "OMX: " args)
#define printk_inf(args...) printk(KERN_INFO "OMX: " args)

#endif /* __omx_xen_debug_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */

