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


SUBDIRS =

# arch=64
if ON_ARCHSIZE_64

# arch=64 / multilib
if OMX_MULTILIB

# arch=64 / multilib / debug build
if OMX_LIB_DEBUG
  SUBDIRS += lib64-debug lib32-debug
endif

# arch=64 / multilib / release build
if OMX_LIB_RELEASE
  SUBDIRS += lib64 lib32
endif

# arch=64 / no multilib
else

# arch=64 / no multilib / debug build
if OMX_LIB_DEBUG
  SUBDIRS += lib64-debug
endif

# arch=64 / no multilib / release build
if OMX_LIB_RELEASE
  SUBDIRS += lib64
endif

# arch=64 / { multilib | no multilib }
endif

# arch=32
else

# arch=32 / debug build
if OMX_LIB_DEBUG
  SUBDIRS += lib32-debug
endif

# arch=32 / release build
if OMX_LIB_RELEASE
  SUBDIRS += lib32
endif

# arch={32|64}
endif

noinst_HEADERS = dlmalloc.h omx_hal.h omx_lib.h	omx__mx_compat.h	\
		 omx_raw.h omx_request.h omx_segments.h omx_threads.h	\
		 omx_types.h omx_valgrind.h omx_list.h omx_debug.h

EXTRA_DIST = omx__mx_lib.version
