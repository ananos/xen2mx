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

#include <sys/ioctl.h>

#include "omx_lib.h"

/**********************
 * Hostname Management
 */

omx_return_t
omx__driver_set_hostname(uint32_t board_index, const char *hostname)
{
  struct omx_cmd_set_hostname set_hostname;
  int err;

  set_hostname.board_index = board_index;
  strncpy(set_hostname.hostname, hostname, OMX_HOSTNAMELEN_MAX);
  set_hostname.hostname[OMX_HOSTNAMELEN_MAX-1] = '\0';

  err = ioctl(omx__globals.control_fd, OMX_CMD_SET_HOSTNAME, &set_hostname);
  if (err < 0) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
							  OMX_INTERNAL_MISC_EINVAL,
							  OMX_ACCESS_DENIED,
							  OMX_SUCCESS,
							  "set hostname");
    if (ret == OMX_INTERNAL_MISC_EINVAL)
      ret = OMX_BOARD_NOT_FOUND;
    return ret;
  }

  return OMX_SUCCESS;
}

omx_return_t
omx__driver_clear_peer_names(void)
{
  int err;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_TABLE_CLEAR_NAMES);
  if (err < 0)
    return omx__ioctl_errno_to_return_checked(OMX_ACCESS_DENIED,
					      OMX_SUCCESS,
					      "clear peer names");

  return OMX_SUCCESS;
}

/************************
 * Peer Table Management
 */

omx_return_t
omx__driver_peer_add(uint64_t board_addr, const char *hostname)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  peer_info.board_addr = board_addr;
  if (hostname)
    strncpy(peer_info.hostname, hostname, OMX_HOSTNAMELEN_MAX);
  else
    peer_info.hostname[0] = '\0';

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_ADD, &peer_info);
  if (err < 0) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_ACCESS_DENIED,
							  OMX_BUSY,
							  OMX_NO_SYSTEM_RESOURCES,
							  OMX_SUCCESS,
							  "add peer to driver table");
    /* let the caller handle errors */
    return ret;
  }

  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  return OMX_SUCCESS;
}

omx_return_t
omx__driver_peers_clear(void)
{
  int err;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_TABLE_CLEAR);
  if (err < 0) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_ACCESS_DENIED,
							  OMX_SUCCESS,
							  "clear driver peer table");
    /* let the caller handle errors */
    return ret;
  }

  return 0;
}

omx_return_t
omx__driver_get_peer_table_state(uint32_t *status, uint32_t *version,
				 uint32_t *size, uint64_t *mapper_id)
{
  struct omx_cmd_peer_table_state state;
  int err;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_TABLE_GET_STATE, &state);
  if (err < 0) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_SUCCESS,
							  "get peer table state");
    /* let the caller handle errors */
    return ret;
  }

  if (status)
    *status = state.status;
  if (version)
    *version = state.version;
  if (size)
    *size = state.size;
  if (mapper_id)
    *mapper_id = state.mapper_id;
  return OMX_SUCCESS;
}

omx_return_t
omx__driver_set_peer_table_state(uint32_t configured, uint32_t version,
				 uint32_t size, uint64_t mapper_id)
{
  struct omx_cmd_peer_table_state state;
  int err;

  state.status = configured ? OMX_PEER_TABLE_STATUS_CONFIGURED : 0;
  state.version = version;
  state.size = size;
  state.mapper_id = mapper_id;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_TABLE_SET_STATE, &state);
  if (err < 0) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_ACCESS_DENIED,
							  OMX_SUCCESS,
							  "set peer table state");
    /* let the caller handle errors */
    return ret;
  }

  return OMX_SUCCESS;
}

/************************
 * Low-Level Peer Lookup
 */

static INLINE omx_return_t
omx__driver_peer_from_index(uint32_t index, uint64_t *board_addr, char *hostname)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  peer_info.index = index;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_FROM_INDEX, &peer_info);
  if (err < 0) {
    omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
				       OMX_SUCCESS,
				       "lookup peer by index");
    /* let the caller handle errors */
    return OMX_PEER_NOT_FOUND;
  }

  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  if (board_addr)
    *board_addr = peer_info.board_addr;
  if (hostname)
    strncpy(hostname, peer_info.hostname, OMX_HOSTNAMELEN_MAX);

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__driver_peer_from_addr(uint64_t board_addr, char *hostname, uint32_t *index)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  peer_info.board_addr = board_addr;

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_FROM_ADDR, &peer_info);
  if (err < 0) {
    omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
				       OMX_SUCCESS,
				       "lookup peer by addr");
    /* let the caller handle errors */
    return OMX_PEER_NOT_FOUND;
  }

  OMX_VALGRIND_MEMORY_MAKE_READABLE(&peer_info, sizeof(peer_info));

  if (index)
    *index = peer_info.index;
  if (hostname)
    strncpy(hostname, peer_info.hostname, OMX_HOSTNAMELEN_MAX);

  return OMX_SUCCESS;
}

static INLINE omx_return_t
omx__driver_peer_from_hostname(const char *hostname, uint64_t *board_addr, uint32_t *index)
{
  struct omx_cmd_misc_peer_info peer_info;
  int err;

  strncpy(peer_info.hostname, hostname, OMX_HOSTNAMELEN_MAX);

  err = ioctl(omx__globals.control_fd, OMX_CMD_PEER_FROM_HOSTNAME, &peer_info);
  if (err < 0) {
    omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
				       OMX_SUCCESS,
				       "lookup peer by hostname");
    /* let the caller handle errors */
    return OMX_PEER_NOT_FOUND;
  }

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
    char raw_hostname[OMX_HOSTNAMELEN_MAX];
    char *hostname = raw_hostname;
    uint64_t board_addr = 0;
    char addr_str[OMX_BOARD_ADDR_STRLEN];

    ret = omx__driver_peer_from_index(i, &board_addr, raw_hostname);
    if (ret != OMX_SUCCESS)
      continue;

    omx__board_addr_sprintf(addr_str, board_addr);
    if (raw_hostname[0] == '\0')
      hostname = "<unknown>";
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
    /* let the caller handle errors */
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
    /* let the caller handle errors */
    return ret;

  *board_addrp = board_addr;
  return OMX_SUCCESS;
}

/* API omx_hostname_to_nic_id */
omx_return_t
omx_hostname_to_nic_id(char *hostname,
		       uint64_t *board_addr)
{
  omx_return_t ret;

  ret = omx__driver_peer_from_hostname(hostname, board_addr, NULL);

  if (ret != OMX_SUCCESS) {
    omx__debug_assert(ret == OMX_PEER_NOT_FOUND);
    return omx__error(OMX_PEER_NOT_FOUND, "hostname_to_nic_id %s",
		      hostname);
  } else {
    return OMX_SUCCESS;
  }
}

/* API omx_nic_id_to_hostname */
omx_return_t
omx_nic_id_to_hostname(uint64_t board_addr,
		       char *hostname)
{
  omx_return_t ret;

  ret = omx__driver_peer_from_addr(board_addr, hostname, NULL);

  if (ret != OMX_SUCCESS) {
    omx__debug_assert(ret == OMX_PEER_NOT_FOUND);
    return omx__error(OMX_PEER_NOT_FOUND, "nic_id_to_hostname %016llx",
		      (unsigned long long) board_addr);
  } else {
    return OMX_SUCCESS;
  }
}
