# Open-MX
# Copyright © inria 2007-2010 (see AUTHORS file)
#
# The development of this software has been funded by Myricom, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or (at
# your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU Lesser General Public License in COPYING.LGPL for more details.


ARCHSIZE	 = 32
AM_CPPFLAGS_SPEC = -DOMX_LIB_DEBUG=1 $(DBGCPPFLAGS)
AM_CFLAGS_SPEC   = -O0 -g -gdwarf-2 -g3 $(DBGCFLAGS)
AM_LDFLAGS_SPEC  = $(DBGLDFLAGS)

if OMX_LIB_COVERAGE
  AM_CFLAGS_SPEC   += -ftest-coverage -fprofile-arcs
endif

if OMX_MULTILIB
  AM_CFLAGS_SPEC   += -m32
  AM_LDFLAGS_SPEC  += -m32
endif

LIBIDIR_SUFFIX=/debug

include ../Makefile-generic.am

