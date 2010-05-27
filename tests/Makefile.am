AM_CPPFLAGS = -I$(abs_top_srcdir)/common -imacros $(abs_top_builddir)/common/config.h	\
	      $(GLOBAL_AM_CPPFLAGS)
AM_CFLAGS   = $(GLOBAL_AM_CFLAGS) -ggdb
AM_LDFLAGS  = $(GLOBAL_AM_LDFLAGS)

testdir = $(bindir)/tests

test_PROGRAMS	= omx_cancel_test omx_cmd_bench omx_loopback_test omx_many	\
		  omx_perf omx_rails omx_rcache_test omx_reg omx_truncated_test	\
		  omx_unexp_handler_test omx_unexp_test omx_vect_test omx_gen_events

omx_reg_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_cmd_bench_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_gen_events_CPPFLAGS = -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_gen_events_CFLAGS   = $(HWLOC_CFLAGS)

LDADD = $(abs_top_builddir)/libopen-mx/$(DEFAULT_LIBDIR)/libopen-mx.la
omx_gen_events_LDADD    = -lpthread $(HWLOC_LIBS) $(LDADD)
