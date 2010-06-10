AM_CPPFLAGS = -I$(abs_top_srcdir)/common -imacros $(abs_top_builddir)/common/config.h	\
	      $(GLOBAL_AM_CPPFLAGS)
AM_CFLAGS   = $(GLOBAL_AM_CFLAGS)
AM_LDFLAGS  = $(GLOBAL_AM_LDFLAGS)

testdir		= $(bindir)/tests
batterydir	= $(testdir)/battery

test_PROGRAMS	= omx_cancel_test omx_cmd_bench omx_loopback_test omx_many	\
		  omx_perf omx_rails omx_rcache_test omx_reg omx_truncated_test	\
		  omx_unexp_handler_test omx_unexp_test omx_vect_test

nodist_battery_SCRIPTS	= battery/omx_test_iface_lo
nodist_battery_DATA	= battery/common.sh

EXTRA_DIST = battery/omx_test_iface_lo.in

omx_reg_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_cmd_bench_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)

LDADD = $(abs_top_builddir)/libopen-mx/$(DEFAULT_LIBDIR)/libopen-mx.la
