AM_CPPFLAGS = -I$(abs_top_srcdir)/common/mx/translate		\
	      -I$(abs_top_srcdir)/common			\
	      -imacros $(abs_top_builddir)/common/config.h	\
	      $(GLOBAL_AM_CPPFLAGS)
AM_CFLAGS   = $(GLOBAL_AM_CFLAGS)
AM_LDFLAGS  = $(GLOBAL_AM_LDFLAGS)

testdir = $(bindir)/tests

test_PROGRAMS	= mx_msg_loop mx_pingpong mx_stream mx_wait_any_test
noinst_HEADERS	= mx_auto_config.h mx_byteswap.h test_common.h

LDADD = $(abs_top_builddir)/libopen-mx/$(DEFAULT_LIBDIR)/libopen-mx.la

mx_stream_LDADD		 = -lpthread $(LDADD)

if OMX_LIB_THREAD_SAFETY
  test_PROGRAMS		+= mx_wakeup_test
  mx_wakeup_test_LDADD	 = -lpthread $(LDADD)
endif

