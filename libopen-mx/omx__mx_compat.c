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

/*
 * This file is shipped within the Open-MX library when the MX API compat is
 * enabled in order to expose MX symbols names since they may be required.
 * Known cases include:
 * + MPICH-MX using &mx_test
 * + Open MPI configure script looking for mx_finalize in libmyriexpress.so
 */

#include "open-mx.h"

void
mx_finalize(void)
{
  omx_finalize();
}

omx_return_t
mx_test(struct omx_endpoint *ep, omx_request_t * request,
	struct omx_status *status, uint32_t * result)
{
  return omx_test(ep, request, status, result);
}
