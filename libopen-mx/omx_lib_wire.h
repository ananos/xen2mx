/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

union omx__truc_data {
  uint8_t type;
  struct omx__truc_ack_data {
    uint8_t type;
    uint8_t pad;
    uint16_t lib_seqnum;
    uint32_t session_id;
    uint32_t acknum;
    uint16_t send_seq;
    uint8_t resent;
    uint8_t pad1;
  } ack;
};

enum omx__truc_data_type {
  OMX__TRUC_DATA_TYPE_ACK = 0x55,
};

#endif /* __omx_lib_wire_h__ */
