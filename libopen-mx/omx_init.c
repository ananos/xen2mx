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
#include <stdlib.h>
#include <sys/mman.h>

#include "omx_lib.h"

struct omx__globals omx__globals = { 0 };
struct omx_driver_desc * omx__driver_desc = NULL;

omx_return_t
omx__init_api(int api)
{
  omx_return_t ret;
  char *env;
  int err;

  if (omx__globals.initialized)
    return OMX_ALREADY_INITIALIZED;

  err = open(OMX_DEVNAME, O_RDONLY);
  if (err < 0)
    return omx__errno_to_return("init open control fd");

  omx__globals.control_fd = err;

  omx__driver_desc = mmap(NULL, OMX_DRIVER_DESC_SIZE, PROT_READ, MAP_SHARED,
			  omx__globals.control_fd, OMX_DRIVER_DESC_FILE_OFFSET);
  if (omx__driver_desc == MAP_FAILED) {
    ret = omx__errno_to_return("mmap driver descriptor");
    goto out_with_fd;
  }

  omx__globals.ack_delay = omx__driver_desc->hz / 100 + 1;
  omx__globals.resend_delay = omx__driver_desc->hz / 2 + 1;
  omx__globals.retransmits_max = 1000;

  omx__globals.regcache = 0;
  env = getenv("OMX_RCACHE");
  if (env)
    omx__globals.regcache = 1;

  omx__globals.verbose = 0;
  env = getenv("OMX_VERBOSE");
  if (env)
    omx__globals.verbose = 1;

  omx__globals.waitspin = 1; /* FIXME: default until wait takes care of the progression timeout */
  env = getenv("OMX_WAITSPIN");
  if (env)
    omx__globals.waitspin = atoi(env);

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
