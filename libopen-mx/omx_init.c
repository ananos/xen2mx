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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "omx_lib.h"

#define OMX_ZOMBIE_MAX_DEFAULT 512

struct omx__globals omx__globals = { 0 };
volatile struct omx_driver_desc * omx__driver_desc = NULL;

/* API omx__init_api */
omx_return_t
omx__init_api(int api)
{
  omx_return_t ret;
  char *env;
  int err;

  if (omx__globals.initialized) {
    ret = OMX_ALREADY_INITIALIZED;
    goto out;
  }

  /*********************************
   * Open, map and check the driver
   */

  err = open(OMX_DEVNAME, O_RDONLY);
  if (err < 0) {
    ret = omx__errno_to_return();
    if (ret == OMX_INTERNAL_UNEXPECTED_ERRNO)
      ret = omx__error(OMX_BAD_ERROR, "Opening global control device (%m)");
    else if (ret == OMX_INTERNAL_MISC_ENODEV)
      ret = omx__error(OMX_NO_DRIVER, "Opening endpoint control device");
    else
      ret = omx__error(ret, "Opening global control device");
    goto out;
  }
  omx__globals.control_fd = err;

  omx__driver_desc = mmap(NULL, OMX_DRIVER_DESC_SIZE, PROT_READ, MAP_SHARED,
			  omx__globals.control_fd, OMX_DRIVER_DESC_FILE_OFFSET);
  if (omx__driver_desc == MAP_FAILED) {
    ret = omx__errno_to_return();
    if (ret == OMX_INTERNAL_MISC_ENODEV || ret == OMX_INTERNAL_UNEXPECTED_ERRNO)
      ret = omx__error(OMX_BAD_ERROR, "Mapping global control device (%m)");
    else
      ret = omx__error(ret, "Mapping global control device");
    goto out_with_fd;
  }

  if (omx__driver_desc->abi_version != OMX_DRIVER_ABI_VERSION) {
    ret = omx__error(omx__driver_desc->abi_version < OMX_DRIVER_ABI_VERSION ? OMX_BAD_KERNEL_ABI : OMX_BAD_LIB_ABI,
		     "Comparing library (ABI 0x%x) with driver (ABI 0x%x)",
		     OMX_DRIVER_ABI_VERSION, omx__driver_desc->abi_version);
    goto out_with_fd;
  }

  /*****************
   * Misc constants
   */

  omx__globals.ack_delay_jiffies = omx__ack_delay_jiffies();
  omx__globals.resend_delay_jiffies = omx__resend_delay_jiffies();

  /*******************************************
   * Verbose and debug messages configuration
   */

  /* verbose message configuration */
  omx__globals.verbose = 0;
#ifdef OMX_LIB_DEBUG
  omx__globals.verbose = 1;
#endif
  env = getenv("OMX_VERBOSE");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_VERBOSE");
    if (env) {
      printf("Emulating MX_VERBOSE as OMX_VERBOSE\n");
      env = "";
    }
  }
#endif
  if (env)
    omx__globals.verbose = atoi(env);

  /* verbose debug message configuration */
  omx__globals.verbdebug = 0;
#ifdef OMX_LIB_DEBUG
  env = getenv("OMX_VERBDEBUG");
  if (env) {
    char *next;
    unsigned long val = strtoul(env, &next, 0);
    if (env == next) {
      int i;
      val = omx__globals.verbdebug;
      for(i=0; env[i]!='\0'; i++) {
	switch (env[i]) {
	case 'P': val |= OMX_VERBDEBUG_ENDPOINT; break;
	case 'C': val |= OMX_VERBDEBUG_CONNECT; break;
	case 'S': val |= OMX_VERBDEBUG_SEND; break;
	case 'L': val |= OMX_VERBDEBUG_LARGE; break;
	case 'M': val |= OMX_VERBDEBUG_MEDIUM; break;
	case 'Q': val |= OMX_VERBDEBUG_SEQNUM; break;
	case 'R': val |= OMX_VERBDEBUG_RECV; break;
	case 'U': val |= OMX_VERBDEBUG_UNEXP; break;
	case 'E': val |= OMX_VERBDEBUG_EARLY; break;
	case 'A': val |= OMX_VERBDEBUG_ACK; break;
	case 'T': val |= OMX_VERBDEBUG_EVENT; break;
	case 'W': val |= OMX_VERBDEBUG_WAIT; break;
	case 'V': val |= OMX_VERBDEBUG_VECT; break;
	default: omx__abort("Unknown verbose debug character '%c'\n", env[i]);
	}
      }
    }
    omx__globals.verbdebug = val;
  }
#endif /* OMX_LIB_DEBUG */

  /**********************************************
   * Shared and self communication configuration
   */

  /* self comm configuration */
#ifndef OMX_DISABLE_SELF
  omx__globals.selfcomms = 1;
  env = getenv("OMX_DISABLE_SELF");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_DISABLE_SELF");
    if (env)
      omx__verbose_printf("Emulating MX_DISABLE_SELF as OMX_DISABLE_SELF\n");
  }
#endif
  if (env) {
    omx__globals.selfcomms = !atoi(env);
    omx__verbose_printf("Forcing self comms to %s\n",
			omx__globals.selfcomms ? "enabled" : "disabled");
  }
#endif

  /* shared comm configuration */
#ifndef OMX_DISABLE_SHARED
  omx__globals.sharedcomms = 1;
  env = getenv("OMX_DISABLE_SHARED");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_DISABLE_SHMEM");
    if (env)
      omx__verbose_printf("Emulating MX_DISABLE_SHMEM as OMX_DISABLE_SHARED\n");
  }
#endif
  if (env) {
    omx__globals.sharedcomms = !atoi(env);
    omx__verbose_printf("Forcing shared comms to %s\n",
			omx__globals.sharedcomms ? "enabled" : "disabled");
  }
#endif

  /*******************************
   * Retransmission configuration
   */

  /* resend configuration */
  omx__globals.req_resends_max = 1000;
  env = getenv("OMX_RESENDS_MAX");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_MAX_RETRIES");
    if (env)
      omx__verbose_printf("Emulating MX_MAX_RETRIES as OMX_RESENDS_MAX\n");
  }
#endif
  if (env) {
    omx__globals.req_resends_max = atoi(env);
    omx__verbose_printf("Forcing resends max to %ld\n", (unsigned long) omx__globals.req_resends_max);
  }

  /* zombie send configuration */
  omx__globals.zombie_max = OMX_ZOMBIE_MAX_DEFAULT;
  env = getenv("OMX_ZOMBIE_SEND");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_ZOMBIE_SEND");
    if (env)
      omx__verbose_printf("Emulating MX_ZOMBIE_SEND as OMX_ZOMBIE_SEND\n");
  }
#endif
  if (env) {
    omx__globals.zombie_max = atoi(env);
    omx__verbose_printf("Forcing zombie max to %d\n",
			omx__globals.zombie_max);
  }

  /* immediate acking threshold */
  omx__globals.not_acked_max = 4;
  env = getenv("OMX_NOTACKED_MAX");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_IMM_ACK");
    if (env)
      omx__verbose_printf("Emulating MX_IMM_ACK as OMX_NOTACKED_MAX\n");
  }
#endif
  if (env) {
    omx__globals.not_acked_max = atoi(env);
    omx__verbose_printf("Forcing immediate acking threshold to %d\n",
			omx__globals.not_acked_max);
  }

  /*************************
   * Sleeping configuration
   */

  /* waitspin configuration */
  omx__globals.waitspin = 0;
  env = getenv("OMX_WAITSPIN");
  /* could be enabled by MX_MONOTHREAD */
  if (env) {
    omx__globals.waitspin = atoi(env);
    omx__verbose_printf("Forcing waitspin to %s\n",
			omx__globals.waitspin ? "enabled" : "disabled");
  }

  /* interrupted wait configuration */
  omx__globals.waitintr = 0;
  env = getenv("OMX_WAITINTR");
  if (env) {
    omx__globals.waitintr = atoi(env);
    omx__verbose_printf("Forcing interrupted wait to %s\n",
			omx__globals.waitintr ? "exit as timeout" : "go back to sleep");
  }

  /*************************
   * Regcache configuration
   */
  omx__globals.regcache = 0;
  env = getenv("OMX_RCACHE");
#ifdef OMX_MX_API_COMPAT
  if (!env) {
    env = getenv("MX_RCACHE");
    if (env)
      omx__verbose_printf("Emulating MX_RCACHE as OMX_RCACHE\n");
  }
#endif
  if (env) {
    omx__globals.regcache = atoi(env);
    omx__verbose_printf("Forcing regcache to %s\n",
			omx__globals.regcache ? "enabled" : "disabled");
  }

  /***************************
   * Terminate initialization
   */

  omx__init_endpoint_list();

  omx__init_error_handler();
  omx__globals.initialized = 1;
  return OMX_SUCCESS;

 out_with_fd:
  close(omx__globals.control_fd);
 out:
  return ret;
}

/* API omx_finalize */
omx_return_t
omx_finalize(void)
{
  /* FIXME: check that it is initialized */

  /* FIXME: check that no endpoint is still open */

  close(omx__globals.control_fd);

  omx__globals.initialized = 0;
  return OMX_SUCCESS;
}
