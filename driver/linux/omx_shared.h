/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

#ifndef __omx_shared_h__
#define __omx_shared_h__

extern int
omx_shared_try_send_connect_request(struct omx_endpoint *src_endpoint,
				    struct omx_cmd_send_connect_request *hdr);

extern int
omx_shared_try_send_connect_reply(struct omx_endpoint *src_endpoint,
				  struct omx_cmd_send_connect_reply *hdr);

extern int
omx_shared_send_tiny(struct omx_endpoint *src_endpoint,
		     struct omx_cmd_send_tiny_hdr *hdr, void __user * data);

extern int
omx_shared_send_small(struct omx_endpoint *src_endpoint,
		      struct omx_cmd_send_small *hdr);

extern int
omx_shared_send_mediumsq_frag(struct omx_endpoint *src_endpoint,
			      struct omx_cmd_send_mediumsq_frag *hdr);

extern int
omx_shared_send_mediumva(struct omx_endpoint *src_endpoint,
			 struct omx_cmd_send_mediumva *hdr);

extern int
omx_shared_send_rndv(struct omx_endpoint *src_endpoint,
		     struct omx_cmd_send_rndv *hdr);

extern int
omx_shared_pull(struct omx_endpoint *src_endpoint,
		struct omx_cmd_pull *hdr);

extern int
omx_shared_send_notify(struct omx_endpoint *src_endpoint,
		       struct omx_cmd_send_notify *hdr);

extern int
omx_shared_send_truc(struct omx_endpoint *src_endpoint,
		     struct omx_cmd_send_truc_hdr *hdr, void __user * data);

#endif /* __omx_shared_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */

