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
# General informations on the project

AC_INIT(Open-MX,
	1.4.0.90,
	open-mx-devel@lists.gforge.inria.fr,
	open-mx,
	http://open-mx.org)

AC_PREREQ(2.65)

AC_PREFIX_DEFAULT(/opt/open-mx)
AC_CONFIG_SRCDIR(./configure.ac)
AC_CONFIG_AUX_DIR(./build-aux)
AC_CONFIG_MACRO_DIR(./build-aux)

# Make the build output less verbose
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Preserve order options in help
m4_ifdef([AC_PRESERVE_HELP_ORDER], [AC_PRESERVE_HELP_ORDER])


PUSH_BUILDENV


# Retrieve the canonical system type
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET


# Guess the arch size (temporary hack before finding a cleaner solution)
AC_COMPILE_IFELSE([AC_LANG_SOURCE([int a [[sizeof(void *)-7]];])],
                  [NATARCHSIZE=64; ALTARCHSIZE=32], [NATARCHSIZE=32; ALTARCHSIZE=64])

AC_SUBST(NATARCHSIZE)
AC_SUBST(ALTARCHSIZE)
AM_CONDITIONAL(ON_ARCHSIZE_64, test x$NATARCHSIZE = x64)


# Activate Automake support
AM_INIT_AUTOMAKE
AC_CONFIG_HEADER(./common/config.h)


# Check for usual build tools
AC_PROG_CPP
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LN_S

m4_ifdef([LT_PREREQ], [],
	 [m4_fatal([libtool version 2 or higher is required], [63])])
LT_INIT
LT_LANG([C])
AC_PROG_INSTALL


POP_BUILDENV


# Global build tool options
GLOBAL_AM_CPPFLAGS="-D_GNU_SOURCE=1"
GLOBAL_AM_CFLAGS="-Wall"
GLOBAL_AM_LDFLAGS="-Wl,--enable-new-dtags"
AC_SUBST(GLOBAL_AM_CPPFLAGS)
AC_SUBST(GLOBAL_AM_CFLAGS)
AC_SUBST(GLOBAL_AM_LDFLAGS)


# Build debug options
AC_ARG_VAR(DBGCPPFLAGS, Additional preprocessor flags used in debug mode)
AC_ARG_VAR(DBGCFLAGS,   Additional compiler flags used in debug mode)
AC_ARG_VAR(DBGLDFLAGS,  Additional linker flags used in debug mode)


# Define the build string
GET_BUILD_STRING(BUILD_STRING)
AC_DEFINE_UNQUOTED(OMX_BUILD_STR, "$BUILD_STRING", Define to the build version string)



# Driver configuration
######################
OMX_DISABLE(driver-build, enable_driver_build,
	    do not build the driver,
	    whether the driver has to be built)

AC_ARG_WITH(linux-release,
	    AC_HELP_STRING(--with-linux-release=<version>, release number of the target kernel),
	    OMX_LINUX_RELEASE=$withval   ; linux_release_set=true,
	    OMX_LINUX_RELEASE=`uname -r` ; linux_release_set=false)
AC_ARG_WITH(linux,
	    AC_HELP_STRING(--with-linux=<dir>, location of the Linux kernel sources),
	    linux_build_set=true ; OMX_LINUX_SRC=$withval; OMX_LINUX_BUILD=$withval,
	    linux_build_set=false)
AC_ARG_WITH(linux-build,
	    AC_HELP_STRING(--with-linux-build=<dir>, location of the Linux kernel build tree),
	    linux_build_set=true ; OMX_LINUX_BUILD=$withval,
	    linux_build_set=false)


test -z $OMX_LINUX_SRC && OMX_LINUX_SRC=/lib/modules/$OMX_LINUX_RELEASE/source
test -z $OMX_LINUX_BUILD && OMX_LINUX_BUILD=/lib/modules/$OMX_LINUX_RELEASE/build

OMX_WITH(shared-ring-entries, n, OMX_SHARED_RING_ENTRIES,
         [change the number of entries per shared ring (default is 1024)],
	 setting the number of shared ring entries to $OMX_SHARED_RING_ENTRIES,
	 default)

AC_ARG_VAR(KCC,	    Compiler used for building the driver)
AC_ARG_VAR(KCFLAGS, Additional kernel compiler flags)
AC_ARG_VAR(KARCH,   Architecture used to build the driver)


AM_CONDITIONAL(OMX_BUILD_DRIVER, test x$enable_driver_build = xyes)


OMX_DEFINE(OMX_SHARED_RING_ENTRY_NR, ${OMX_SHARED_RING_ENTRIES}UL,
	   Define to change the number of entries per shared ring,
	   test x$OMX_SHARED_RING_ENTRIES != xdefault)


if test x$enable_driver_build = xyes ; then
  OMX_PROG_KCC($KCC)
  OMX_FIND_KERNEL_HEADERS(OMX_LINUX_HDR, $OMX_LINUX_SRC $OMX_LINUX_BUILD)
  OMX_FIND_KERNEL_AUTOCONF_HEADER(OMX_LINUX_AUTOCONF_DIR, $OMX_LINUX_BUILD $OMX_LINUX_SRC)
  OMX_CHECK_KBUILD_MAKEFILE($OMX_LINUX_BUILD)

  OMX_GET_KERNEL_RELEASE(OMX_LINUX_REAL_RELEASE, $OMX_LINUX_BUILD)
  AC_MSG_NOTICE(found Linux kernel release $OMX_LINUX_REAL_RELEASE in build tree)

  # If the user forces the Linux release, this will be the value used when
  # installing the module.
  #
  # Else, Linux release tries to be inferred from the Linux build tree (if this
  # one has been forced. Else `uname -r` is used). See the comments below.
  warn_about_kernelrelease=false
  if $linux_release_set ; then
    if test x$OMX_LINUX_RELEASE != "x$OMX_LINUX_REAL_RELEASE" ; then
      # Here, the user forces the Linux release but this value conflicts with
      # the one inferred from the build tree. We keep the user value because the
      # autodetected one can be buggy (see the comment below).
      # Nonetheless, we issue a warning related to this conflict.
      AC_MSG_WARN(kernel build reports $OMX_LINUX_REAL_RELEASE instead of $OMX_LINUX_RELEASE)
      AC_MSG_WARN(installing in $OMX_LINUX_RELEASE as requested)
    fi
  else
    if $linux_build_set ; then
      # Here, Linux release has not been specified but the build tree has
      # been. So, the only usable value is the one inferred from the build
      # tree. Most of the time, this value will matches the `uname -r` value
      # when running this kernel. So things work well because we use `uname -r`
      # in omx_init to find the module location.
      #
      # But sometimes, some kernels (like some Debian ones) have a buggy 'make
      # kernelrelease' which differs from the `uname -r` of this kernel. Such
      # case will lead to omx_init being unable to find the kernel module.
      #
      # Whatever, we have no choice here to keep the 'make kernelrelease'
      # value. But, we inform the user at the end of the configure with an
      # emphasized message about this possible issue as well as with the
      # associated workaround: force the Linux release to match `uname -r`.
      AC_MSG_NOTICE(installing in $OMX_LINUX_REAL_RELEASE as detected in the build tree)
      OMX_LINUX_RELEASE="$OMX_LINUX_REAL_RELEASE"
      warn_about_kernelrelease=true
    fi
  fi

  { ! test -z $KCC && _kcc=$KCC; } || _kcc=$CC
  OMX_GET_GCC_VERSION(GCC_VERSION, $_kcc)
  OMX_GET_KERNEL_GCC_VERSION(KERNEL_GCC_VERSION)

  if test "x$OMX_LINUX_RELEASE" = x`uname -r` ; then
      if test "x$GCC_VERSION" != "x$KERNEL_GCC_VERSION" ; then
	  AC_MSG_WARN(kernel built with gcc $KERNEL_GCC_VERSION and Open-MX with $GCC_VERSION)
      fi
  fi

  OMX_CHECK_KERNEL_HEADERS($OMX_LINUX_BUILD, $OMX_LINUX_HDR, $OMX_LINUX_RELEASE)
  OMX_FIND_KBUILD_CFLAGS_ARG(OMX_KBUILD_CFLAGS_ARG, $OMX_LINUX_BUILD)
  OMX_SYMLINK_DRIVER_SOURCES

  AC_SUBST(OMX_KBUILD_CFLAGS_ARG)
  AC_SUBST(OMX_LINUX_RELEASE)
  AC_SUBST(OMX_LINUX_HDR)
  AC_SUBST(OMX_LINUX_BUILD)   # useful ? --> in the driver Makefile !?
fi


test -z $KCC   || OMX_KBUILD_ARGS="$OMX_KBUILD_ARGS CC=\"$KCC $KCFLAGS\""
test -z $KARCH || OMX_KBUILD_ARGS="$OMX_KBUILD_ARGS ARCH=\"$KARCH\""
AC_SUBST(OMX_KBUILD_ARGS)



# Driver debugging configuration
################################
OMX_DISABLE(counters, enable_counters,
            disable counters support in the driver,
	    whether counter support is enabled in the driver)

AC_ARG_ENABLE(recvcopy,
	      AC_HELP_STRING(--disable-recvcopy,
			     [do not copy data on the receive side, pass random data to user-space]),
	      enable_recvcopy=$enableval,
	      enable_recvcopy=yes)



OMX_DEFINE(OMX_DRIVER_COUNTERS, 1,
	   Define to enable countering in the driver,
   	   test x$enable_counters = xyes)
OMX_DEFINE(OMX_NORECVCOPY, 1,
	   Define to remove the driver copy on the receive side and pass random data to user-space,
	   test x$enable_recvcopy = xno)



if test x$enable_recvcopy = xno ; then
    AC_MSG_NOTICE(random data will be passed by the driver to user-space on the receive side)
fi





# Library configuration
#######################
OMX_DISABLE(library-build, enable_library_build,
	    do not build the library and user-space tools,
            whether the library and user-space tools have to be built)
 OMX_ENABLE(multilib, enable_multilib,
            build both a 32-bits and 64-bits version of the library,
            whether both a 32-bits and 64-bits version of the library have to be built)
OMX_DISABLE(internal-malloc, enable_internal_malloc,
	    use the regular malloc instead of a custom internal dlmalloc if
	    there is no risky symbol interception,
	    whether internal malloc is enabled)
OMX_DISABLE(threads, enable_lib_threads,
	    disable thread safety in the library,
            whether thread safety is enabled in the library)


AM_CONDITIONAL(OMX_BUILD_LIBRARY,     test x$enable_library_build   = xyes)
AM_CONDITIONAL(OMX_MULTILIB,          test x$enable_multilib        = xyes)
AM_CONDITIONAL(OMX_LIB_DLMALLOC,      test x$enable_internal_malloc = xyes)
AM_CONDITIONAL(OMX_LIB_THREAD_SAFETY, test x$enable_lib_threads     = xyes)


OMX_DEFINE(OMX_LIB_DLMALLOC, 1, Define to enable the internal dlmalloc,
	   test x$enable_internal_malloc = xyes)
OMX_DEFINE(OMX_LIB_THREAD_SAFETY, 1, Define to enable thread safety support in the library,
 	   test x$enable_lib_threads = xyes)



# Library debugging configuration
#################################
AC_ARG_ENABLE(debug,
	      AC_HELP_STRING(--enable-debug, build the library and the driver in debug mode),
	      enable_debug=$enableval,
	      enable_debug=both)

AC_ARG_ENABLE(valgrind,
	      AC_HELP_STRING(--disable-valgrind, disable Valgrind hooks in the debug library),
	      enable_valgrind=$enableval,
	      enable_valgrind=maybe)


AM_CONDITIONAL(OMX_LIB_DEBUG,   test x$enable_debug    = xyes || test x$enable_debug = xboth)
AM_CONDITIONAL(OMX_LIB_RELEASE, test x$enable_debug    = xno  || test x$enable_debug = xboth)

OMX_DRIVER_DEBUG=0
DEFAULT_LIBDIR=lib$NATARCHSIZE
if test x$enable_debug = xyes ; then
    AC_MSG_NOTICE(building the library in debug mode)
    AC_MSG_NOTICE(building the driver in debug mode)

    DEFAULT_LIBDIR=${DEFAULT_LIBDIR}-debug

    OMX_DRIVER_DEBUG=1
    OMX_DEFINE(OMX_DRIVER_DEBUG, 1,
	       Define to enable debugging features in the driver,
	       test x$enable_debug = xyes)
elif test x$enable_debug = xno ; then
    AC_MSG_NOTICE(building the library in release mode)
elif test x$enable_debug = xboth ; then
    AC_MSG_NOTICE(building the library in both debug and release mode)
fi
AC_SUBST(OMX_DRIVER_DEBUG)
AC_SUBST(DEFAULT_LIBDIR)

if test x$enable_valgrind != xno ; then
   OMX_CHECK_VALGRIND_HEADERS(valgrind_found)
   if $valgrind_found ; then
       AC_DEFINE(OMX_LIB_VALGRIND_DEBUG, 1, Enable memory allocation debugging with Valgrind in the debug library)
       AC_MSG_NOTICE(activating Valgrind hooks in the debug library)
   fi
fi





# MX compatibility and wire configuration
#########################################
OMX_DISABLE(endian, enable_endian,
	    disable endian-independant networking,
	    whether endian-independant networking is enabled)
 OMX_ENABLE(mx-wire, enable_mx_wire,
            enable MX wire compatibility,
	    whether MX wire compatibility is enabled)
OMX_DISABLE(mx-abi, enable_mx_abi,
            disable MX binary compatibility,
	    whether MX ABI compatibility is enabled)

OMX_WITH_COND(mtu, n, OMX_MTU,
	      [change MTU (default is 9000 in non-MX-wire-compatible mode)],
	      setting MTU to $OMX_MTU, test x$enable_mx_wire = xno,
	      9000)
OMX_WITH_COND(medium-frag-length, n, OMX_MEDIUM_FRAG_LENGTH,
	      [change medium fragment maximum payload size (default depends on the MTU and wire
	       compatibility)],
	       setting medium frag length to $OMX_MEDIUM_FRAG_LENGTH, test x$enable_mx_wire = xno,
	       default)
OMX_WITH_COND(medium-frags, n, OMX_MEDIUM_FRAGS_MAX,
	      [change the maximal number of fragments per medium message (default is 32 in
	       non-MX-wire-compatible mode)],
	      setting the maximal number of fragments per medium message to $OMX_MEDIUM_FRAGS_MAX,
	      test x$enable_mx_wire = xno,
	      default)
OMX_WITH_COND(pull-reply-length, n, OMX_PULL_REPLY_LENGTH,
	      [change pull reply maximum payload size (default depends on the MTU and wire
	       compatibility)],
	       setting pull reply length to $OMX_PULL_REPLY_LENGTH, test x$enable_mx_wire = xno,
	       default)
OMX_WITH_COND(pull-block-replies, n, OMX_PULL_BLOCK_REPLIES,
	      [change the number of pull replies per block (default is 32 in non-MX-wire-compatible
	       mode)],
	       setting pull block replies to $OMX_PULL_BLOCK_REPLIES, test x$enable_mx_wire = xno,
	       default)
OMX_WITH_COND(ethertype, n, OMX_ETHERTYPE,
	      [change Ethertype (default is 0x86DF)],
	      setting Ethertype to $OMX_ETHERTYPE, test x$enable_mx_wire = xno,
	      default)

AM_CONDITIONAL(OMX_MX_ABI_COMPAT, test x$enable_mx_abi = xyes)

OMX_DEFINE(OMX_MX_ABI_COMPAT, 1, Define to enable MX ABI compatibility,
           test x$enable_mx_abi = xyes)
OMX_DEFINE(OMX_MX_WIRE_COMPAT, 1, Define to enable MX wire compatibilty,
           test x$enable_mx_wire = xyes)
OMX_DEFINE(OMX_MEDIUM_FRAG_LENGTH_MAX, $OMX_MEDIUM_FRAG_LENGTH,
	   Define to change the medium frag length max,
	   test x$OMX_MEDIUM_FRAG_LENGTH != xdefault)
OMX_DEFINE(OMX_MEDIUM_FRAGS_MAX, ${OMX_MEDIUM_FRAGS_MAX},
	   Define to change the maximal number of fragments per medium message,
	   test x$OMX_MEDIUM_FRAGS_MAX != xdefault)
OMX_DEFINE(OMX_PULL_REPLY_LENGTH_MAX, $OMX_PULL_REPLY_LENGTH,
	   Define to change the pull reply length max,
	   test x$OMX_PULL_REPLY_LENGTH != xdefault)
OMX_DEFINE(OMX_PULL_REPLY_PER_BLOCK, $OMX_PULL_BLOCK_REPLIES,
           Define to change the number of pull replies per block,
	   test x$OMX_PULL_BLOCK_REPLIES != xdefault)
OMX_DEFINE(ETH_P_OMX, $OMX_ETHERTYPE, Define to change the Ethertype,
	   test x$OMX_ETHERTYPE != xdefault)
OMX_DEFINE(OMX_ENDIAN_COMPAT, 1, Define to enable endian-independent networking,
	   test x$enable_endian = xyes)

if test x$enable_mx_wire = xno ; then
    AC_DEFINE_UNQUOTED(OMX_MTU, $OMX_MTU, Define to change the MTU)
fi

# MX wire compatibility is incompatible with many tunning options
if test x$enable_mx_wire = xyes ; then
   if test x$OMX_MTU != x9000 ; then
      AC_MSG_ERROR(Changing the MTU requires MX-wire compatibility to be disabled)
   fi
   if test x$OMX_MEDIUM_FRAG_LENGTH != xdefault ; then
      AC_MSG_ERROR(Changing the medium frag length max requires MX-wire compatibility to be
      	           disabled)
   fi
   if test x$OMX_MEDIUM_FRAGS_MAX != xdefault ; then
      AC_MSG_ERROR(Changing the maximal number of medium fragments requires MX-wire
      	           compatibility to be disabled)
   fi
   if test x$OMX_PULL_REPLY_LENGTH != xdefault ; then
      AC_MSG_ERROR(Changing the number of pull replies per block requires MX-wire
      	           compatibility to be disabled)
   fi
   if test x$OMX_PULL_BLOCK_REPLIES != xdefault ; then
      AC_MSG_ERROR(Changing the number of pull replies per block requires MX-wire compatibility
      		   to be disabled)
   fi
   if test x$OMX_ETHERTYPE != xdefault ; then
      AC_MSG_ERROR(Changing the Ethertype requires MX-wire compatibility to be disabled)
   fi
   if test x$enable_endian = xno ; then
      AC_MSG_WARN(endian-independant networking is required to talk to native MX peers)
   fi
fi





# Peer discovery
################
OMX_DISABLE(fma, enable_fma,
	    never use FMA,
	    whether FMA is enabled)
 OMX_ENABLE(static-peers, enable_static_peers,
 	    use static peer instead of dynamic discovery,
            whether static peer mapping is performed instead of dynamic)

OMX_WITH_COND(peers-file, path, OMX_PEERS_FILE,
	      use argument as a static peers file,
	      using the static peers file $OMX_PEERS_FILE, test x$enable_static_peers = xyes,
	      /etc/open-mx/peers)


if test x$enable_static_peers = xyes ; then
    AC_SUBST(OMX_STATIC_PEERS, 1)
else
    AC_SUBST(OMX_STATIC_PEERS, 0)
fi

if test x$enable_static_peers != xyes ; then
   OMX_PEER_DISCOVERY=omxoed
   if test x$enable_fma = xyes ; then
      AC_MSG_CHECKING(whether FMA sources are available)
      if test -d "$srcdir/fma" ; then
	 AC_MSG_RESULT(yes)
	 if test x$enable_mx_abi = xyes ; then
	    OMX_PEER_DISCOVERY=fma
	 else
	    AC_MSG_ERROR(building FMA requires MX ABI compatibility)
	 fi
      else
	 AC_MSG_RESULT(no)
      fi
   fi

   if test x$OMX_PEER_DISCOVERY = xfma ; then
      AC_MSG_NOTICE(using FMA for dynamic peer discovery)
      mkdir -p mcp
      AC_CONFIG_SUBDIRS(fma) # Beurk...
   else
      AC_MSG_NOTICE(using omxoed for dynamic peer discovery)
      if test x$enable_mx_wire = xyes && test x$enable_fma = xyes ; then
	 AC_MSG_ERROR([FMA is recommended for dynamic discovery of native MX peers, use --disable-fma to force])
      fi
   fi
fi

AM_CONDITIONAL(OMX_USE_FMA, test x$OMX_PEER_DISCOVERY = xfma)

AC_SUBST(OMX_PEERS_FILE)
AC_SUBST(OMX_PEER_DISCOVERY)





# FMS specific config
#####################
OMX_WITH_COND(fms-run, dir, OMX_FMS_RUN,
	      directory where fms data are to be stored,
	      using $OMX_FMS_RUN to store fms data, test x$OMX_PEER_DISCOVERY = xfma,
	      /var/run/fms)
OMX_WITH_COND(fms-server, fms_server, OMX_FMS_SERVER,
	      node on which fms process will run,
	      fms will run on $OMX_FMS_SERVER, test x$OMX_PEER_DISCOVERY = xfma,
	      default)

AC_SUBST(OMX_FMS_RUN)

test x$OMX_FMS_SERVER = xdefault && OMX_FMS_SERVER=
AC_SUBST(OMX_FMS_SERVER)





# Device nodes
##############
OMX_WITH(device, path:group:mode, OMX_DEVICE,
         [[change main device node name, group and mode]],
	 setting device to $OMX_DEVICE,
	 open-mx:root:0666)
OMX_WITH(raw-device, path:group:mode, OMX_RAW_DEVICE,
         [[change raw device node name, group and mode]],
	 setting raw device to $OMX_RAW_DEVICE,
	 open-mx-raw:root:0666)


OMX_MAIN_DEVICE_NAME=$(echo $OMX_DEVICE  | cut -d: -f1)
OMX_MAIN_DEVICE_GROUP=$(echo $OMX_DEVICE | cut -d: -f2 -s)
OMX_MAIN_DEVICE_MODE=$(echo $OMX_DEVICE  | cut -d: -f3 -s)

OMX_DEFINE(OMX_MAIN_DEVICE_NAME, "$OMX_MAIN_DEVICE_NAME",
           Define to change the main device node name,
	   ! test -z $OMX_MAIN_DEVICE_NAME)
OMX_DEFINE(OMX_MAIN_DEVICE_GROUP, "$OMX_MAIN_DEVICE_GROUP",
	   Define to change the main device group owner,
	   ! test -z $OMX_MAIN_DEVICE_GROUP)
OMX_DEFINE(OMX_MAIN_DEVICE_MODE, "$OMX_MAIN_DEVICE_MODE",
	   Define to change the main device permissions,
	   ! test -z $OMX_MAIN_DEVICE_MODE)

AC_SUBST(OMX_MAIN_DEVICE_NAME)
AC_SUBST(OMX_MAIN_DEVICE_GROUP)
AC_SUBST(OMX_MAIN_DEVICE_MODE)


OMX_RAW_DEVICE_NAME=$(echo $OMX_RAW_DEVICE  | cut -d: -f1)
OMX_RAW_DEVICE_GROUP=$(echo $OMX_RAW_DEVICE | cut -d: -f2 -s)
OMX_RAW_DEVICE_MODE=$(echo $OMX_RAW_DEVICE  | cut -d: -f3 -s)

OMX_DEFINE(OMX_RAW_DEVICE_NAME, "$OMX_RAW_DEVICE_NAME",
	   Define to change the raw device node name,
	   ! test -z $OMX_RAW_DEVICE_NAME)
OMX_DEFINE(OMX_RAW_DEVICE_GROUP, "$OMX_RAW_DEVICE_GROUP",
	   Define to change the raw device group owner,
	   ! test -z $OMX_RAW_DEVICE_GROUP)
OMX_DEFINE(OMX_RAW_DEVICE_MODE, "$OMX_RAW_DEVICE_MODE",
	   Define to change the raw device permissions,
	   ! test -z $OMX_RAW_DEVICE_MODE)

AC_SUBST(OMX_RAW_DEVICE_NAME)
AC_SUBST(OMX_RAW_DEVICE_GROUP)
AC_SUBST(OMX_RAW_DEVICE_MODE)


# Test configuration
# Do not use multiline for the both following variables
TEST_LIST='loopback_native loopback_shared loopback_self unexpected unexpected_with_ctxids unexpected_handler truncated wait_any cancel wakeup multirails monothread_wait_any multithread_wait_any vect_native vect_shared vect_self pingpong_native pingpong_shared randomloop'

BATTERY_LIST='loopback misc vect pingpong'

FINAL_TEST_LIST=
for t in $TEST_LIST ; do FINAL_TEST_LIST="$FINAL_TEST_LIST launchers/$t" ; done

AC_SUBST(TEST_LIST)
AC_SUBST(FINAL_TEST_LIST)
AC_SUBST(BATTERY_LIST)





# External dependencies
########################

# hwloc support relies on the PKG_CHECK_MODULE macro which is usually shipped
# with pkgconfig. So, if pkgconfig is not installed, just skip all the hwloc
# stuff.
m4_ifdef([PKG_CHECK_MODULES], [
AC_ARG_ENABLE(hwloc,
	      [AS_HELP_STRING([--disable-hwloc], [disable hwloc in the user-space tools])],
	      enable_hwloc=$enableval, enable_hwloc=yes)
if test x$enable_hwloc = xyes; then
  PKG_CHECK_MODULES(HWLOC, hwloc >= 1.0, :, enable_hwloc=no)
  if test x$enable_hwloc = xyes; then
    # use pkg-config result to check that headers and libs are usable
    # (in case it's a cross compilation environment)
    omx_cflags_save="$CFLAGS"
    omx_libs_save="$LIBS"
    CFLAGS="$CFLAGS $HWLOC_CFLAGS"
    LIBS="$LIBS $HWLOC_LIBS"
    AC_CHECK_FUNC(hwloc_topology_init, :, enable_hwloc=no)
    CFLAGS="$omx_cflags_save"
    LIBS="$omx_libs_save"
    if test x$enable_hwloc = xno; then
      # clear pkg-config result if hwloc isn't actually usable
      HWLOC_CFLAGS=
      HWLOC_LIBS=
    fi
  fi
fi
AM_CONDITIONAL(OMX_HAVE_HWLOC, test x$enable_hwloc = xyes)
if test x$enable_hwloc = xyes; then
  AC_DEFINE(OMX_HAVE_HWLOC, 1, [Define to enable hwloc in user-space tools])
fi
])


AC_PATH_PROG(modprobe_path, modprobe, /sbin/modprobe, dnl
	     [$PATH$PATH_SEPARATOR/usr/sbin$PATH_SEPARATOR/sbin])
AC_PATH_PROG(modinfo_path, modinfo, /sbin/modprobe, dnl
	     [$PATH$PATH_SEPARATOR/usr/sbin$PATH_SEPARATOR/sbin])
AC_PATH_PROG(insmod_path, insmod, /sbin/modprobe, dnl
	     [$PATH$PATH_SEPARATOR/usr/sbin$PATH_SEPARATOR/sbin])
AC_PATH_PROG(rmmod_path, rmmod, /sbin/rmmod, dnl
	     [$PATH$PATH_SEPARATOR/usr/sbin$PATH_SEPARATOR/sbin])

if test x$modprobe_path = x ; then
   AC_MSG_ERROR([No modprobe executable found])
fi
if test x$modinfo_path = x ; then
   AC_MSG_ERROR([No modinfo executable found])
fi
if test x$insmod_path, = x ; then
   AC_MSG_ERROR([No insmod executable found])
fi
if test x$rmmod_path, = x ; then
   AC_MSG_ERROR([No rmmod executable found])
fi

AC_SUBST(MODPROBE_PATH, $modprobe_path)
AC_SUBST(MODINFO_PATH, $modinfo_path)
AC_SUBST(INSMOD_PATH, $insmod_path)
AC_SUBST(RMMOD_PATH, $rmmod_path)


# Here is the list of configured files
AC_CONFIG_FILES(Makefile
		driver/linux/Makefile
		driver/linux/Kbuild
		libopen-mx/Makefile
		libopen-mx/lib32/Makefile
		libopen-mx/lib64/Makefile
		libopen-mx/lib32-debug/Makefile
		libopen-mx/lib64-debug/Makefile
		tools/Makefile
		tools/omx_check
		tools/omx_local_install
		tools/omx_init
		tools/10-open-mx.rules
		tests/Makefile
		tests/mx/Makefile
		tests/helpers/omx_test_launcher)

AC_SUBST(CONFIG_STATUS_DEPENDENCIES,
	'$(top_srcdir)/driver/linux/Kbuild.in $(top_srcdir)/driver/linux/check_kernel_headers.sh')

AC_CONFIG_COMMANDS(chmoding-omx_check,		chmod ugo+x tools/omx_check)
AC_CONFIG_COMMANDS(chmoding-omx_init,		chmod ugo+x tools/omx_init)
AC_CONFIG_COMMANDS(chmoding-omx_test_launcher,	chmod ugo+x tests/helpers/omx_test_launcher)

OMX_SYMLINK_TESTS

AC_OUTPUT

# Create the DKMS quick configure file
AC_MSG_NOTICE(Creating the DKMS quick configure script)
cat >configure-dkms <<EOF
$srcdir/configure $(./config.status --config) "\$@"
EOF
chmod ugo+x configure-dkms

cat <<EOF

SUMMARY
*******
Linux kernel sources:	 $OMX_LINUX_SRC
Linux kernel build tree: $OMX_LINUX_BUILD
Linux kernel release:	 $OMX_LINUX_RELEASE
EOF

if $warn_about_kernelrelease ; then
cat <<EOF

WARNING
*******
You specified a Linux build tree (through --with-linux or --with-linux-build)
without having specified a Linux release (with --with-linux-release).
Most of the time, this will work as the kernel release inferred from the build
tree will match the \`uname -r\` (which is used at runtime) one when running this
kernel.
But some buggy kernels (like some Debian ones) report a wrong kernel release
from their build tree. This will lead to omx_init being unable to find the
kernel module location.
If this happen, rebuild Open-MX by forcing the Linux release
(using --with-linux-release) so that it matches the \`uname -r\` value of this kernel.
EOF
fi
