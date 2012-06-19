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

#ifndef __omx_xen_lib_h__
#define __omx_xen_lib_h__

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/ring.h>
#include <linux/cdev.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include "omx_io.h"
#include "omx_wire.h"
#include "omx_xen.h"

void dump_xen_recv_tiny(struct omx_cmd_xen_recv_msg *info);
void dump_xen_send_tiny(struct omx_cmd_xen_send_tiny *info);
void dump_xen_pull(struct omx_cmd_xen_pull *info);
void dump_xen_recv_liback(struct omx_cmd_xen_recv_liback *info);
void dump_xen_send_notify(struct omx_cmd_xen_send_notify *info);
void dump_xen_recv_notify(struct omx_cmd_xen_recv_msg *info);
void dump_xen_send_liback(struct omx_cmd_xen_send_liback *info);
void dump_xen_recv_pull_done(struct omx_cmd_xen_recv_pull_done *info);
void dump_xen_recv_pull_request(struct omx_cmd_xen_recv_pull_request *info);
void dump_xen_send_rndv(struct omx_cmd_xen_send_rndv *info);
void dump_xen_recv_msg(struct omx_cmd_xen_recv_msg *info);
void dump_xen_recv_connect_request(struct omx_cmd_xen_recv_connect_request
				   *info);
void dump_xen_recv_connect_reply(struct omx_cmd_xen_recv_connect_reply *info);
void dump_xen_send_connect_request(struct omx_cmd_xen_send_connect_request
				   *info);
void dump_xen_send_connect_reply(struct omx_cmd_xen_send_connect_reply *info);
void dump_xen_get_board_info(struct omx_cmd_xen_get_board_info *info);
void dump_xen_get_endpoint_info(struct omx_cmd_xen_get_endpoint_info *info);
void dump_xen_set_hostname(struct omx_cmd_xen_set_hostname *info);
void dump_xen_misc_peer_info(struct omx_cmd_xen_misc_peer_info *info);
void dump_xen_bench(struct omx_cmd_xen_bench *info);
void dump_xen_peer_table_state(struct omx_cmd_xen_peer_table_state *info);
void dump_xen_ring_msg_register_user_segment(struct
					     omx_ring_msg_register_user_segment
					     *info);
void dump_xen_ring_msg_deregister_user_segment(struct
					       omx_ring_msg_deregister_user_segment
					       *info);
void dump_xen_ring_msg_create_user_region(struct omx_ring_msg_create_user_region
					  *info);
void dump_xen_ring_msg_destroy_user_region(struct
					   omx_ring_msg_destroy_user_region
					   *info);
void dump_xen_ring_msg_endpoint(struct omx_ring_msg_endpoint *info);

#endif				/* __omx_xen_lib_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
