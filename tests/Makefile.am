# Open-MX
# Copyright © INRIA 2007-2011 (see AUTHORS file)
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

RM_F  = rm -f

AM_CPPFLAGS = -I$(abs_top_srcdir)/common -imacros $(abs_top_builddir)/common/config.h	\
	      $(GLOBAL_AM_CPPFLAGS)
AM_CFLAGS   = $(GLOBAL_AM_CFLAGS)
AM_LDFLAGS  = $(GLOBAL_AM_LDFLAGS)

testdir		= $(bindir)/tests
batterydir	= $(testdir)/battery
helpersdir	= $(testdir)/helpers
launchersdir	= $(testdir)/launchers

test_PROGRAMS		= omx_cancel_test omx_cmd_bench omx_loopback_test omx_many	\
			  omx_perf omx_rails omx_rcache_test omx_reg omx_truncated_test	\
			  omx_unexp_handler_test omx_unexp_test omx_vect_test		\
			  omx_multithread_wait_any omx_endpoint_addr_context_test

dist_helpers_SCRIPTS	= helpers/omx_test_double_app helpers/omx_test_battery
nodist_helpers_SCRIPTS	= helpers/omx_test_launcher

TESTS			= $(FINAL_TEST_LIST)

omx_reg_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_cmd_bench_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)

LDADD = $(abs_top_builddir)/libopen-mx/$(DEFAULT_LIBDIR)/libopen-mx.la

omx_multithread_wait_any_CFLAGS	= $(HWLOC_CFLAGS)
omx_multithread_wait_any_LDADD	= $(HWLOC_LIBS) -lpthread $(LDADD)

install-data-hook:
	@mkdir -p $(DESTDIR)$(launchersdir)
	@for t in $(TEST_LIST) ;							\
	    do 										\
	    $(LN_S) -f ../helpers/omx_test_launcher $(DESTDIR)$(launchersdir)/$$t ;	\
	    done
	@mkdir -p $(DESTDIR)/$(batterydir)
	@for b in $(BATTERY_LIST) ;							\
	    do										\
	    $(LN_S) -f ../helpers/omx_test_battery $(DESTDIR)$(batterydir)/$$b ;	\
	    done

uninstall-local:
	@for t in $(TEST_LIST)    ; do $(RM_F) $(DESTDIR)$(launchersdir)/$$t ; done
	@for b in $(BATTERY_LIST) ; do $(RM_F) $(DESTDIR)$(batterydir)/$$b ; done

