/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include "errno.h"

#include "omx_lib.h"

/******************************
 * Endpoint address management
 */

omx_return_t
omx_get_endpoint_addr(omx_endpoint_t endpoint,
		      omx_endpoint_addr_t *endpoint_addr)
{
  omx__partner_to_addr(endpoint->myself, endpoint_addr);
  return OMX_SUCCESS;
}

omx_return_t
omx_decompose_endpoint_addr(omx_endpoint_addr_t endpoint_addr,
			    uint64_t *nic_id, uint32_t *endpoint_id)
{
  struct omx__partner *partner = omx__partner_from_addr(&endpoint_addr);
  *nic_id = partner->board_addr;
  *endpoint_id = partner->endpoint_index;
  return OMX_SUCCESS;
}

/*********************
 * Partner management
 */

omx_return_t
omx__partner_create(struct omx_endpoint *ep, uint16_t peer_index,
		    uint64_t board_addr, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  struct omx__partner * partner;
  uint32_t partner_index;

  partner = malloc(sizeof(*partner));
  if (!partner) {
    return omx__errno_to_return(ENOMEM, "partner malloc");
  }

  partner->board_addr = board_addr;
  partner->endpoint_index = endpoint_index;
  partner->peer_index = peer_index;
  INIT_LIST_HEAD(&partner->partialq);
  partner->next_send_seq = 0;
  partner->next_match_recv_seq = 0;
  partner->next_frag_recv_seq = 0;

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__globals.endpoint_max;
  ep->partners[partner_index] = partner;

  *partnerp = partner;

  return OMX_SUCCESS;
}

omx_return_t
omx__partner_lookup(struct omx_endpoint *ep,
		    uint64_t board_addr, uint8_t endpoint_index,
		    struct omx__partner ** partnerp)
{
  uint32_t partner_index;
  uint16_t peer_index;
  omx_return_t ret;

  ret = omx__peer_addr_to_index(board_addr, &peer_index);
  if (ret != OMX_SUCCESS) {
    char board_addr_str[OMX_BOARD_ADDR_STRLEN];
    omx__board_addr_sprintf(board_addr_str, board_addr);
    fprintf(stderr, "Failed to find peer index of board %s (%s)\n",
	    board_addr_str, omx_strerror(ret));
    return ret;
  }

  partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__globals.endpoint_max;

  if (!ep->partners[partner_index])
    return omx__partner_create(ep, peer_index, board_addr, endpoint_index, partnerp);


  *partnerp = ep->partners[partner_index];
  return OMX_SUCCESS;
}

/*************
 * Connection
 */

omx_return_t
omx_connect(omx_endpoint_t ep,
	    uint64_t nic_id, uint32_t endpoint_id, uint32_t key,
	    uint32_t timeout,
	    omx_endpoint_addr_t *addr)
{
  /* FIXME: do a real roundtrip to check the key, exchange session
   * and src_peer_index and initialize receiver's seqnums
   */
  struct omx__partner * partner;
  omx_return_t ret;

  ret = omx__partner_lookup(ep, nic_id, endpoint_id, &partner);
  if (ret != OMX_SUCCESS)
    return ret;

  omx__partner_to_addr(partner, addr);
  return OMX_SUCCESS;
}
