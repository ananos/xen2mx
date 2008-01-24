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

#include <linux/kernel.h>

#include "omx_endpoint.h"
#include "omx_iface.h"
#include "omx_common.h"
#include "omx_io.h"

int
omx_shared_connect(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		   struct omx_cmd_send_connect_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_connect event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.shared = 1;
	event.length = hdr->length;
	event.seqnum = hdr->seqnum;

	/* copy the data */
	err = copy_from_user(&event.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send connect cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_CONNECT, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_CONNECT);
	omx_counter_inc(dst_endpoint->iface, RECV_CONNECT);

	return 0;

 out:
	return err;
}

int
omx_shared_tiny(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		struct omx_cmd_send_tiny_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_msg event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.tiny.length = hdr->length;

	/* copy the data */
	err = copy_from_user(&event.specific.tiny.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send tiny cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_TINY, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_TINY);
	omx_counter_inc(dst_endpoint->iface, RECV_TINY);

	return 0;

 out:
	return err;
}

int
omx_shared_rndv(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		struct omx_cmd_send_rndv_hdr *hdr, void __user * data)
{
	struct omx_evt_recv_msg event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.match_info = hdr->match_info;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.rndv.length = hdr->length;

	/* copy the data */
	err = copy_from_user(&event.specific.rndv.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send rndv cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_RNDV, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_RNDV);
	omx_counter_inc(dst_endpoint->iface, RECV_RNDV);

	return 0;

 out:
	return err;
}

int
omx_shared_notify(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		  struct omx_cmd_send_notify *hdr)
{
	struct omx_evt_recv_msg event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.seqnum = hdr->seqnum;
	event.piggyack = hdr->piggyack;
	event.specific.notify.length = hdr->total_length;
	event.specific.notify.puller_rdma_id = hdr->puller_rdma_id;
	event.specific.notify.puller_rdma_seqnum = hdr->puller_rdma_seqnum;

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_NOTIFY, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_NOTIFY);
	omx_counter_inc(dst_endpoint->iface, RECV_NOTIFY);

	return 0;

 out:
	return err;
}

int
omx_shared_truc(struct omx_endpoint *src_endpoint, struct omx_endpoint *dst_endpoint,
		struct omx_cmd_send_truc *hdr, void __user * data)
{
	struct omx_evt_recv_truc event;
	int err;

	event.peer_index = src_endpoint->iface->peer.index;
	event.src_endpoint = src_endpoint->endpoint_index;
	event.length = hdr->length;

	/* copy the data */
	err = copy_from_user(&event.data, data, hdr->length);
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read shared send truc cmd data\n");
		err = -EFAULT;
		goto out;
	}

	/* notify the event */
	err = omx_notify_unexp_event(dst_endpoint, OMX_EVT_RECV_TRUC, &event, sizeof(event));
	if (unlikely(err < 0)) {
		/* no more unexpected eventq slot? just drop the packet, it will be resent anyway */
		goto out;
	}

	omx_counter_inc(src_endpoint->iface, SEND_TRUC);
	omx_counter_inc(dst_endpoint->iface, RECV_TRUC);

	return 0;

 out:
	return err;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
