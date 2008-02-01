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

#include "omx_io.h"
#include "omx_lib.h"

/*
 * Returns the current amount of boards attached to the driver
 */
omx_return_t
omx__get_board_count(uint32_t * count)
{
  omx_return_t ret = OMX_SUCCESS;
  int err;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    goto out;
  }

  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_COUNT, count);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl GET_BOARD_COUNT");
    goto out;
  }

  OMX_VALGRIND_MEMORY_MAKE_READABLE(count, sizeof(*count));

 out:
  return ret;
}

/*
 * Returns the board id of the endpoint if non NULL,
 * or the current board corresponding to the index.
 *
 * index, addr, hostname and ifacename pointers may be NULL is unused.
 */
omx_return_t
omx__get_board_id(struct omx_endpoint * ep, uint8_t * index,
		  uint64_t * addr, char * hostname, char * ifacename)
{
  omx_return_t ret = OMX_SUCCESS;
  struct omx_cmd_get_board_info board_info;
  int err, fd;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    goto out;
  }

  if (ep) {
    /* use the endpoint fd */
    fd = ep->fd;
  } else {
    /* use the control fd and the index */
    fd = omx__globals.control_fd;
    board_info.board_index = *index;
  }

  err = ioctl(fd, OMX_CMD_GET_BOARD_INFO, &board_info);
  if (err < 0) {
    ret = omx__errno_to_return("ioctl GET_BOARD_INFO");
    goto out;
  }
  OMX_VALGRIND_MEMORY_MAKE_READABLE(board_info.info.hostname, OMX_HOSTNAMELEN_MAX);
  OMX_VALGRIND_MEMORY_MAKE_READABLE(board_info.info.ifacename, OMX_IF_NAMESIZE);
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&board_info.info.board_addr, sizeof(board_info.info.board_addr));
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&board_info.board_index, sizeof(board_info.board_index));

  if (index)
    *index = board_info.board_index;
  if (hostname)
    strncpy(hostname, board_info.info.hostname, OMX_HOSTNAMELEN_MAX);
  if (ifacename)
    strncpy(ifacename, board_info.info.ifacename, OMX_IF_NAMESIZE);
  if (addr)
    *addr = board_info.info.board_addr;

 out:
  return ret;
}

/*
 * Returns the current index of a board given by its name
 */
omx_return_t
omx__get_board_index_by_name(const char * name, uint8_t * index)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t max = omx__driver_desc->board_max;
  int err, i;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    goto out;
  }

  ret = OMX_INVALID_PARAMETER;
  for(i=0; i<max; i++) {
    struct omx_cmd_get_board_info board_info;

    board_info.board_index = i;
    err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_INFO, &board_info);
    if (err < 0) {
      ret = omx__errno_to_return("ioctl GET_BOARD_INFO");
      if (ret != OMX_INVALID_PARAMETER)
	goto out;
    }
    OMX_VALGRIND_MEMORY_MAKE_READABLE(board_info.info.hostname, OMX_HOSTNAMELEN_MAX);

    if (!strncmp(name, board_info.info.hostname, OMX_HOSTNAMELEN_MAX)) {
      ret = OMX_SUCCESS;
      *index = i;
      break;
    }
  }

 out:
  return ret;
}

/*
 * Returns the current index of a board given by its addr
 */
omx_return_t
omx__get_board_index_by_addr(uint64_t addr, uint8_t * index)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t max = omx__driver_desc->board_max;
  int err, i;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    goto out;
  }

  ret = OMX_INVALID_PARAMETER;
  for(i=0; i<max; i++) {
    struct omx_cmd_get_board_info board_info;

    board_info.board_index = i;
    err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_INFO, &board_info);
    if (err < 0) {
      ret = omx__errno_to_return("ioctl GET_BOARD_INFO");
      if (ret != OMX_INVALID_PARAMETER)
	goto out;
    }
    OMX_VALGRIND_MEMORY_MAKE_READABLE(&board_info.info.board_addr, sizeof(board_info.info.board_addr));

    if (addr == board_info.info.board_addr) {
      ret = OMX_SUCCESS;
      *index = i;
      break;
    }
  }

 out:
  return ret;
}

/*
 * Returns various info
 */
omx_return_t
omx_get_info(struct omx_endpoint * ep, enum omx_info_key key,
	     const void * in_val, uint32_t in_len,
	     void * out_val, uint32_t out_len)
{
  switch (key) {
  case OMX_INFO_BOARD_MAX:
    if (!omx__globals.initialized)
      return OMX_NOT_INITIALIZED;
    if (out_len < sizeof(uint32_t))
      return OMX_INVALID_PARAMETER;
    *(uint32_t *) out_val = omx__driver_desc->board_max;
    return OMX_SUCCESS;

  case OMX_INFO_ENDPOINT_MAX:
    if (!omx__globals.initialized)
      return OMX_NOT_INITIALIZED;
    if (out_len < sizeof(uint32_t))
      return OMX_INVALID_PARAMETER;
    *(uint32_t *) out_val = omx__driver_desc->endpoint_max;
    return OMX_SUCCESS;

  case OMX_INFO_BOARD_COUNT:
    if (out_len < sizeof(uint32_t))
      return OMX_INVALID_PARAMETER;
    return omx__get_board_count((uint32_t *) out_val);

  case OMX_INFO_BOARD_HOSTNAME:
  case OMX_INFO_BOARD_IFACENAME:
    if (ep) {
      /* use the info stored in the endpoint */
      if (key == OMX_INFO_BOARD_HOSTNAME)
	strncpy(out_val, ep->hostname, out_len);
      else
	strncpy(out_val, ep->ifacename, out_len);
      return OMX_SUCCESS;

    } else {
      /* if no endpoint given, ask the driver about the index given in in_val */
      uint64_t addr;
      char hostname[OMX_HOSTNAMELEN_MAX];
      char ifacename[OMX_IF_NAMESIZE];
      uint8_t index;
      omx_return_t ret;

      if (!in_val || !in_len)
	return OMX_INVALID_PARAMETER;
      index = *(uint8_t*)in_val;

      ret = omx__get_board_id(ep, &index, &addr, hostname, ifacename);
      if (ret != OMX_SUCCESS)
	return ret;

      if (key == OMX_INFO_BOARD_HOSTNAME)
	strncpy(out_val, hostname, out_len);
      else
	strncpy(out_val, ifacename, out_len);
    }

  default:
    return OMX_INVALID_PARAMETER;
  }

  return OMX_SUCCESS;
}

/*
 * Translate local board number/addr
 */

omx_return_t
omx_board_number_to_nic_id(uint32_t board_number,
			   uint64_t *nic_id)
{
  uint8_t index = board_number;
  return omx__get_board_id(NULL, &index, nic_id, NULL, NULL);
}

omx_return_t
omx_nic_id_to_board_number(uint64_t nic_id,
			   uint32_t *board_number)
{
  omx_return_t ret;
  uint8_t index = -1; /* shut-up the compiler */

  ret = omx__get_board_index_by_addr(nic_id, &index);
  if (ret == OMX_SUCCESS)
    *board_number = index;
  return ret;
}
