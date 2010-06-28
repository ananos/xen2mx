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

/*
 * This file mimics mx_io.h from Myricom's MX distribution.
 * It is used to build applications on top of Open-MX using the MX ABI.
 */

#ifndef MX_IO_H
#define MX_IO_H

typedef int mx_endpt_handle_t;
#define MX__INVALID_HANDLE -1

typedef struct {
  uint32_t board_number;
  uint8_t mapper_mac[6];
  uint16_t iport;
  uint32_t map_version;
  uint32_t num_hosts;
  uint32_t network_configured;
  uint32_t routes_valid;
  uint32_t level;
  uint32_t flags;
} mx_mapper_state_t;

#endif /* MX_IO_H */
