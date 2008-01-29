/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
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

#include <sys/ioctl.h>

#include "omx_lib.h"

/************************
 * Peer Table Management
 */

omx_return_t
omx__driver_peer_add(uint64_t board_addr, char *hostname)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  peer_info.board_addr = board_addr;
  strncpy(peer_info.hostname, hostname, OMX_HOSTNAMELEN_MAX);

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_ADD, &peer_info);
  if (err < 0)
    return omx__errno_to_return("OMX_CMD_PEER_ADD");
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  return OMX_SUCCESS;
}

omx_return_t
omx__driver_peers_clear()
{
  int err;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEERS_CLEAR);
  if (err < 0)
    return omx__errno_to_return("OMX_CMD_PEERS_CLEAR");

  return 0;
}

/************************
 * Low-Level Peer Lookup
 */

static inline omx_return_t
omx__driver_peer_from_index(uint32_t index, uint64_t *board_addr, char *hostname)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  peer_info.index = index;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_FROM_INDEX, &peer_info);
  if (err < 0)
    return omx__errno_to_return("OMX_CMD_PEER_FROM_INDEX");
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  if (board_addr)
    *board_addr = peer_info.board_addr;
  if (hostname)
    strncpy(hostname, peer_info.hostname, OMX_HOSTNAMELEN_MAX);

  return OMX_SUCCESS;
}

static inline omx_return_t
omx__driver_peer_from_addr(uint64_t board_addr, char *hostname, uint32_t *index)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  peer_info.board_addr = board_addr;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_FROM_ADDR, &peer_info);
  if (err < 0)
    return omx__errno_to_return("OMX_CMD_PEER_FROM_ADDR");
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  if (index)
    *index = peer_info.index;
  if (hostname)
    strncpy(hostname, peer_info.hostname, OMX_HOSTNAMELEN_MAX);

  return OMX_SUCCESS;
}

static inline omx_return_t
omx__driver_peer_from_hostname(char *hostname, uint64_t *board_addr, uint32_t *index)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  strncpy(peer_info.hostname, hostname, OMX_HOSTNAMELEN_MAX);

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_FROM_HOSTNAME, &peer_info);
  if (err < 0)
    return omx__errno_to_return("OMX_CMD_PEER_FROM_HOSTNAME");
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  if (index)
    *index = peer_info.index;
  if (board_addr)
    *board_addr = peer_info.board_addr;

  return OMX_SUCCESS;
}

/*************************
 * High-Level Peer Lookup
 */

omx_return_t
omx__peers_dump(const char * format)
{
  omx_return_t ret;
  int i;

  for(i=0; i<omx__driver_desc->peer_max; i++) {
    char hostname[OMX_HOSTNAMELEN_MAX];
    uint64_t board_addr = 0;
    char addr_str[OMX_BOARD_ADDR_STRLEN];

    ret = omx__driver_peer_from_index(i, &board_addr, hostname);
    if (ret != OMX_SUCCESS)
      break;

    omx__board_addr_sprintf(addr_str, board_addr);
    printf(format, i, addr_str, hostname);
  }

  return OMX_SUCCESS;
}

omx_return_t
omx__peer_addr_to_index(uint64_t board_addr, uint16_t *indexp)
{
  omx_return_t ret;
  uint32_t index = -1;

  ret = omx__driver_peer_from_addr(board_addr, NULL, &index);
  if (ret != OMX_SUCCESS)
    return ret;

  *indexp = index;
  return OMX_SUCCESS;
}

omx_return_t
omx__peer_index_to_addr(uint16_t index, uint64_t *board_addrp)
{
  omx_return_t ret;
  uint64_t board_addr = 0;

  ret = omx__driver_peer_from_index(index, &board_addr, NULL);
  if (ret != OMX_SUCCESS)
    return ret;

  *board_addrp = board_addr;
  return OMX_SUCCESS;
}

omx_return_t
omx_hostname_to_nic_id(char *hostname,
		       uint64_t *board_addr)
{
  return omx__driver_peer_from_hostname(hostname, board_addr, NULL);
}

omx_return_t
omx_nic_id_to_hostname(uint64_t board_addr,
		       char *hostname)
{
  return omx__driver_peer_from_addr(board_addr, hostname, NULL);
}
