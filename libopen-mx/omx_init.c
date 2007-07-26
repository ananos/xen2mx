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

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "omx_lib.h"

struct omx__globals omx__globals = { 0 };

omx_return_t
omx__init_api(int api)
{
  omx_return_t ret;
  int err;

  if (omx__globals.initialized)
    return OMX_ALREADY_INITIALIZED;

  err = open(OMX_DEVNAME, O_RDONLY);
  if (err < 0)
    return omx__errno_to_return(errno, "init open control fd");

  omx__globals.control_fd = err;

  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_BOARD_MAX, &omx__globals.board_max);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out_with_fd;
  }

  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_ENDPOINT_MAX, &omx__globals.endpoint_max);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl GET_ENDPOINT_MAX");
    goto out_with_fd;
  }

  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_PEER_MAX, &omx__globals.peer_max);
  if (err < 0) {
    ret = omx__errno_to_return(errno, "ioctl GET_PEER_MAX");
    goto out_with_fd;
  }

  ret = omx__peers_init();
  if (ret != OMX_SUCCESS)
    goto out_with_fd;

  omx__globals.initialized = 1;
  return OMX_SUCCESS;

 out_with_fd:
  close(omx__globals.control_fd);
  return ret;
}

omx_return_t
omx_finalize(void)
{
  /* FIXME: check that no endpoint is still open */

  close(omx__globals.control_fd);

  omx__globals.initialized = 0;
  return OMX_SUCCESS;
}
