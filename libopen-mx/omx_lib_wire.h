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

#ifndef __omx_lib_wire_h__
#define __omx_lib_wire_h__

#include <stdint.h>

struct omx__rndv_data {
  uint32_t msg_length;
  uint8_t rdma_id;
  uint8_t rdma_seqnum;
  uint16_t rdma_offset;
};

struct omx__connect_request_data {
  /* the sender's session id (so that the connected know when the connect has been sent) */
  uint32_t src_session_id;
  /* the application level key in the request that the connected will check */
  uint32_t app_key;
  /* the target next recv seqnum (so the connected knows our next send seqnum) */
  uint16_t target_recv_seqnum_start;
  /* is this a request ot a reply? 0 here */
  uint8_t is_reply;
  /* sequence number of this connect request (in case multiple have been sent/lost) */
  uint8_t connect_seqnum;

  uint8_t pad2;
};

struct omx__connect_reply_data {
  /* the sender's session id (so that we know when the connect has been sent) */
  uint32_t src_session_id;
  /* the target session_id (so that the connecter can send right after this connect) */
  uint32_t target_session_id;
  /* the target next recv seqnum (so that the connecter know our next send seqnum) */
  uint16_t target_recv_seqnum_start;
  /* is this a request ot a reply? 1 here */
  uint8_t is_reply;
  /* sequence number of this connect request (in case multiple have been sent/lost) */
  uint8_t connect_seqnum;
  /* the status code to return in the connecter request */
  uint8_t status_code;
};

/* FIXME: assertions so that is_reply is at the same offset/size */

union omx__truc_data {
  uint8_t type;
  struct omx__truc_ack_data {
    uint8_t type;
    uint8_t pad;
    uint16_t lib_seqnum;
    uint32_t session_id;
    uint32_t acknum;
    uint16_t send_seq;
    uint8_t requeued;
    uint8_t pad1;
  } ack;
};

enum omx__truc_data_type {
  OMX__TRUC_DATA_TYPE_ACK = 0x55,
};

#endif /* __omx_lib_wire_h__ */
