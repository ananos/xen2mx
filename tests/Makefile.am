RM_F  = rm -f

AM_CPPFLAGS = -I$(abs_top_srcdir)/common -imacros $(abs_top_builddir)/common/config.h	\
	      $(GLOBAL_AM_CPPFLAGS)
AM_CFLAGS   = $(GLOBAL_AM_CFLAGS) -ggdb
AM_LDFLAGS  = $(GLOBAL_AM_LDFLAGS)

testdir		= $(bindir)/tests
batterydir	= $(testdir)/battery
helpersdir	= $(testdir)/helpers
launchersdir	= $(testdir)/launchers

test_PROGRAMS	= omx_cancel_test omx_cmd_bench omx_loopback_test omx_many	\
		  omx_perf omx_rails omx_rcache_test omx_reg omx_truncated_test	\
		  omx_unexp_handler_test omx_unexp_test omx_vect_test omx_event_delivery_loopback

dist_helpers_SCRIPTS	= helpers/omx_test_double_app helpers/omx_test_battery
nodist_helpers_SCRIPTS	= helpers/omx_test_launcher

TESTS			= $(FINAL_TEST_LIST)

omx_reg_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_cmd_bench_CPPFLAGS  = -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)
omx_event_delivery_loopback_CPPFLAGS	= -I$(abs_top_srcdir)/libopen-mx $(AM_CPPFLAGS)

omx_event_delivery_loopback_CFLAGS	= $(HWLOC_CFLAGS)

LDADD = $(abs_top_builddir)/libopen-mx/$(DEFAULT_LIBDIR)/libopen-mx.la

omx_event_delivery_loopback_LDADD	= $(HWLOC_LIBS) $(LDADD)

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

