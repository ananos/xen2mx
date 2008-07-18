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
    /* let the caller handle this */
    goto out;
  }

  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_COUNT, count);
  if (err < 0)
    omx__ioctl_errno_to_return_checked(OMX_SUCCESS,
				       "get board count");

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
omx__get_board_info(struct omx_endpoint * ep, uint32_t index, struct omx_board_info * info)
{
  struct omx_cmd_get_board_info get_info;
  omx_return_t ret = OMX_SUCCESS;
  int err, fd;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    /* let the caller handle this */
    goto out;
  }

  if (ep) {
    /* use the endpoint fd */
    fd = ep->fd;
  } else {
    /* use the control fd and the index */
    fd = omx__globals.control_fd;
    get_info.board_index = index;
  }

  err = ioctl(fd, OMX_CMD_GET_BOARD_INFO, &get_info);
  if (err < 0) {
    if (!ep) {
      omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
					 OMX_SUCCESS,
					 "get board info");
      ret = OMX_BOARD_NOT_FOUND;
    } else {
      omx__ioctl_errno_to_return_checked(OMX_SUCCESS,
					 "get board info");
    }
    /* let the caller handle this */
    goto out;
  }
  OMX_VALGRIND_MEMORY_MAKE_READABLE(get_info.info.hostname, OMX_HOSTNAMELEN_MAX);
  OMX_VALGRIND_MEMORY_MAKE_READABLE(get_info.info.ifacename, OMX_IF_NAMESIZE);
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&get_info.info.addr, sizeof(get_info.info.addr));
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&get_info.board_index, sizeof(get_info.board_index));

  memcpy(info, &get_info.info, sizeof(get_info.info));

 out:
  return ret;
}

/*
 * Returns the current index of a board given by its name
 */
omx_return_t
omx__get_board_index_by_name(const char * name, uint32_t * index)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t max = omx__driver_desc->board_max;
  int err, i;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    /* let the caller handle this */
    goto out;
  }

  for(i=0; i<max; i++) {
    struct omx_cmd_get_board_info board_info;

    board_info.board_index = i;
    err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_INFO, &board_info);
    if (err < 0) {
      omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
					 OMX_SUCCESS,
					 "get board info to find index by name");
      continue;
    }
    OMX_VALGRIND_MEMORY_MAKE_READABLE(board_info.info.hostname, OMX_HOSTNAMELEN_MAX);

    if (!strncmp(name, board_info.info.hostname, OMX_HOSTNAMELEN_MAX)) {
      *index = i;
      goto out;
    }
  }
  ret = OMX_BOARD_NOT_FOUND;

 out:
  return ret;
}

/*
 * Returns the current index of a board given by its addr
 */
omx_return_t
omx__get_board_index_by_addr(uint64_t addr, uint32_t * index)
{
  omx_return_t ret = OMX_SUCCESS;
  uint32_t max = omx__driver_desc->board_max;
  int err, i;

  if (!omx__globals.initialized) {
    ret = OMX_NOT_INITIALIZED;
    /* let the caller handle this */
    goto out;
  }

  for(i=0; i<max; i++) {
    struct omx_cmd_get_board_info board_info;

    board_info.board_index = i;
    err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_INFO, &board_info);
    if (err < 0) {
      omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
					 OMX_SUCCESS,
					 "get board info to find index by addr");
      continue;
    }
    OMX_VALGRIND_MEMORY_MAKE_READABLE(&board_info.info.addr, sizeof(board_info.info.addr));

    if (addr == board_info.info.addr) {
      *index = i;
      goto out;
    }
  }
  ret = OMX_BOARD_NOT_FOUND;

 out:
  return ret;
}

/***********************
 * Returns various info
 */

/* API omx_get_info */
omx_return_t
omx_get_info(struct omx_endpoint * ep, enum omx_info_key key,
	     const void * in_val, uint32_t in_len,
	     void * out_val, uint32_t out_len)
{
  /* no need to lock here, there's no possible race condition or so */

  if (out_val == NULL) {
    if (ep)
      return omx__error_with_ep(ep, OMX_BAD_INFO_ADDRESS,
				"Getting info with NULL out value pointer");
    else
      return omx__error(OMX_BAD_INFO_ADDRESS,
			"Getting info with NULL out value pointer");
  }

  switch (key) {
  case OMX_INFO_BOARD_MAX:

    if (!omx__globals.initialized)
      return omx__error(OMX_NOT_INITIALIZED,
			"Getting board max");

    if (out_len < sizeof(uint32_t))
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting board max into %ld bytes instead of %z",
			(unsigned long) out_len, sizeof(uint32_t));

    *(uint32_t *) out_val = omx__driver_desc->board_max;
    return OMX_SUCCESS;

  case OMX_INFO_ENDPOINT_MAX:

    if (!omx__globals.initialized)
      return omx__error(OMX_NOT_INITIALIZED,
			"Getting endpoint max");

    if (out_len < sizeof(uint32_t))
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting endpoint max into %ld bytes instead of %z",
			(unsigned long) out_len, sizeof(uint32_t));

    *(uint32_t *) out_val = omx__driver_desc->endpoint_max;
    return OMX_SUCCESS;

  case OMX_INFO_BOARD_COUNT:

    if (out_len < sizeof(uint32_t))
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting board count max into %ld bytes instead of %z",
			(unsigned long) out_len, sizeof(uint32_t));

    return omx__get_board_count((uint32_t *) out_val);

  case OMX_INFO_BOARD_IDS: {
    uint32_t count, i, j;
    omx_return_t ret;

    ret = omx__get_board_count(&count);
    if (!ret)
      return omx__error(ret, "Getting board count for board ids");

    if (out_len < sizeof (uint64_t) * (count+1))
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting board count into %ld instead of %z",
			(unsigned long) out_len, sizeof (uint64_t) * (count+1));

    for (i = 0, j = 0; i < count && j < omx__driver_desc->board_max; j++) {
      struct omx_board_info tmp;

      ret = omx__get_board_info(ep, j, &tmp);
      if (ret != OMX_SUCCESS)
	return omx__error(ret, "Getting board #%ld id", (unsigned long) j);

      ((uint64_t *) out_val) [i] = tmp.addr;
      i++;
    }

    ((uint64_t *) out_val) [i] = 0;
    return OMX_SUCCESS;
  }

  case OMX_INFO_BOARD_HOSTNAME:
  case OMX_INFO_BOARD_IFACENAME:
  case OMX_INFO_BOARD_NUMA_NODE: {
    struct omx_board_info tmp;
    struct omx_board_info *info = &tmp;

    if (ep) {
      /* use the info stored in the endpoint */
      info = &ep->board_info;

    } else {
      /* if no endpoint given, ask the driver about the index given in in_val */
      uint32_t index;
      omx_return_t ret;

      if (!in_val)
	return omx__error(OMX_BAD_INFO_ADDRESS, "Getting board info for index given at %p", in_val);
      if (!in_len)
	return omx__error(OMX_BAD_INFO_LENGTH, "Getting board info for index given in %ld bytes", (unsigned long) in_len);
      index = *(uint8_t*)in_val;

      ret = omx__get_board_info(ep, index, &tmp);
      if (ret != OMX_SUCCESS)
	return omx__error(ret, "Getting board %ld info", (unsigned long) index);
    }

    if (key == OMX_INFO_BOARD_HOSTNAME) {
      strncpy(out_val, info->hostname, out_len);

    } else if (key == OMX_INFO_BOARD_IFACENAME) {
      strncpy(out_val, info->ifacename, out_len);

    } else if (key == OMX_INFO_BOARD_NUMA_NODE) {
      if (out_len < sizeof(uint32_t))
	return omx__error(OMX_BAD_INFO_LENGTH,
			  "Getting board numa node into %ld bytes instead of %z",
			  (unsigned long) out_len, sizeof(uint32_t));
      *(uint32_t *) out_val = info->numa_node;

    } else {
      assert(0);
    }

    return OMX_SUCCESS;
  }

  case OMX_INFO_COUNTER_MAX:

    if (out_len < sizeof(uint32_t))
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting counter max %ld bytes instead of %z",
			(unsigned long) out_len, sizeof(uint32_t));

    *(uint32_t *) out_val = OMX_COUNTER_INDEX_MAX;
    return OMX_SUCCESS;

  case OMX_INFO_COUNTER_VALUES: {
    struct omx_cmd_get_counters get_counters;
    int err;

    if (out_len < sizeof(uint32_t) * OMX_COUNTER_INDEX_MAX)
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting counter values %ld bytes instead of %z",
			(unsigned long) out_len, sizeof(uint32_t) * OMX_COUNTER_INDEX_MAX);

    get_counters.clear = 0;
    get_counters.buffer_addr = (uintptr_t) out_val;
    get_counters.buffer_length = out_len;

    if (ep)
      get_counters.board_index = ep->board_index;
    else
      get_counters.board_index = *(uint8_t*)in_val;

    err = ioctl(omx__globals.control_fd, OMX_CMD_GET_COUNTERS, &get_counters);
    if (err < 0) {
      omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_INTERNAL_MISC_EINVAL,
							    OMX_ACCESS_DENIED,
							    OMX_SUCCESS,
							    "get counters");
      if (ret == OMX_INTERNAL_MISC_EINVAL)
	ret = OMX_BOARD_NOT_FOUND;
      return omx__error(ret, "Getting counter values");
    }

    return OMX_SUCCESS;
  }

  case OMX_INFO_COUNTER_LABEL: {
    int index = *(uint8_t*)in_val;
    const char *label = omx_strcounter(index);

    if (out_len < strlen(label) + 1)
      return omx__error(OMX_BAD_INFO_LENGTH,
			"Getting counter values %ld bytes instead of %",
			(unsigned long) out_len, strlen(label) + 1);

    strcpy((char *) out_val, label);
    return OMX_SUCCESS;
  }

  default:
    return omx__error(OMX_BAD_INFO_KEY,
		      "Getting info key %ld",
		      (unsigned long) key);;
  }

  return OMX_SUCCESS;
}

/*
 * Translate local board number/addr
 */

/* API omx_board_number_to_nic_id */
omx_return_t
omx_board_number_to_nic_id(uint32_t board_number,
			   uint64_t *nic_id)
{
  struct omx_board_info info;
  uint32_t index = board_number;
  omx_return_t ret;

  ret = omx__get_board_info(NULL, index, &info);
  if (ret == OMX_SUCCESS)
    *nic_id = info.addr;

  return omx__error(ret, "board_number_to_nic_id %ld",
		    (unsigned long) board_number);
}

/* API omx_nic_id_to_board_number */
omx_return_t
omx_nic_id_to_board_number(uint64_t nic_id,
			   uint32_t *board_number)
{
  omx_return_t ret;
  uint32_t index = -1; /* shut-up the compiler */

  ret = omx__get_board_index_by_addr(nic_id, &index);
  if (ret == OMX_SUCCESS)
    *board_number = index;

  return omx__error(ret, "nic_id_to_board_number %llx",
		    (unsigned long long) nic_id);
}
