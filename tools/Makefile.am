# Open-MX
# Copyright © inria 2007-2010 (see AUTHORS file)
#
# The development of this software has been funded by Myricom, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or (at
# your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU General Public License in COPYING.GPL for more details.

AM_CPPFLAGS = -imacros $(abs_top_builddir)/common/config.h		\
	      -I$(abs_top_srcdir)/common -I$(abs_top_srcdir)/libopen-mx	\
	      $(GLOBAL_AM_CPPFLAGS)
AM_CFLAGS   = $(GLOBAL_AM_CFLAGS)
AM_LDFLAGS  = $(GLOBAL_AM_LDFLAGS)

omxconfdir = $(sysconfdir)/open-mx

bin_PROGRAMS	= omx_counters omx_endpoint_info omx_hostname omx_info	\
		  omx_init_peers omxoed omx_prepare_binding
bin_SCRIPTS	= omx_check
sbin_SCRIPTS	= omx_init omx_local_install
omxconf_DATA	= open-mx.conf 10-open-mx.rules

EXTRA_DIST	= open-mx.conf

LDADD	     = $(abs_top_builddir)/libopen-mx/$(DEFAULT_LIBDIR)/libopen-mx.la
omxoed_LDADD = -lpthread $(LDADD)
