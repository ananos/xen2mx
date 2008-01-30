# Open-MX
# Copyright © INRIA 2007-2008 (see AUTHORS file)
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

AC_INIT([Open-MX],
        0.2.0,
        [http://gforge.inria.fr/projects/open-mx],
        open-mx)
AC_PREREQ(2.60)
AC_CANONICAL_SYSTEM
AC_PREFIX_DEFAULT(/opt/open-mx)

########################
# external tools config

AC_PROG_CPP
AC_PROG_CC
AC_PROG_MKDIR_P
AC_PROG_SED
AC_PROG_LN_S
AC_PROG_INSTALL

AC_CHECK_TOOL(AR, ar, false)
if test $AR = false; then
  AC_MSG_ERROR(ar program not found in \$PATH)
fi

if test -n "$KCC" ; then
	AC_CHECK_PROG(KCC, gcc, $CC)
	AC_MSG_CHECKING(which compiler to use for kernel modules)
	AC_MSG_RESULT($KCC)
fi

#####################
# build flags config

AC_SUBST(CPPFLAGS)
AC_SUBST(CFLAGS)

if test -z "$DBGCFLAGS" ; then
	DBGCFLAGS="-g -O0"
fi
AC_SUBST(DBGCFLAGS)

########################
# Kernel related config

LINUX_SRC="/lib/modules/"`uname -r`"/source"
LINUX_BUILD="/lib/modules/"`uname -r`"/build"
# If the user changes the source, use it as build by default
# If it changes the build, do not use it as the source by default
AC_ARG_WITH(linux,
	[  --with-linux=<dir>			location of the Linux kernel source],
	LINUX_SRC="$withval"; LINUX_BUILD="$withval")
AC_ARG_WITH(linux-build,
	[  --with-linux-build=<dir>		location of the Linux kernel build tree],
	LINUX_BUILD="$withval")
AC_ARG_WITH(linux-release,
	[  --with-linux-release=<version>	release number of the target kernel],
	LINUX_RELEASE="$withval",
	LINUX_RELEASE=$(make kernelrelease -C ${LINUX_BUILD} | grep ^2.6.))
AC_SUBST(LINUX_SRC)
AC_SUBST(LINUX_BUILD)
AC_SUBST(LINUX_RELEASE)

# Find where the headers are (to avoid grepping at both places).
# Do not check for autoconf.h or version.h since these are in
# both the source and the build directory.
AC_MSG_CHECKING(kernel header include/linux/kernel.h)
LINUX_HDR=
if test -f ${LINUX_SRC}/include/linux/kernel.h ; then
  LINUX_HDR=$LINUX_SRC
else if test -f ${LINUX_BUILD}/include/linux/kernel.h ; then
  LINUX_HDR=$LINUX_BUILD
fi fi
if test -z ${LINUX_HDR} ; then
  AC_MSG_ERROR(not found)
fi
AC_MSG_RESULT(found in in ${LINUX_HDR})

AC_MSG_CHECKING(kernel build Makefile)
if ! test -f ${LINUX_BUILD}/Makefile ; then
  AC_MSG_ERROR(not found in ${LINUX_BUILD})
fi
AC_MSG_RESULT(found in ${LINUX_BUILD})

# Find the kernel release
AC_MSG_CHECKING(kernel release)
if test x${LINUX_RELEASE} = x; then
  AC_MSG_ERROR(not found)
fi
AC_MSG_RESULT(${LINUX_RELEASE})

# create the output file
CHECKS_NAME=driver/linux/omx_checks.h
CHECKS_PATH=`pwd`/${CHECKS_NAME}
CHECKS_DATE_PREFIX="This file has been first generated on "
TMP_CHECKS_PATH=${CHECKS_PATH}.tmp
rm -f ${TMP_CHECKS_PATH}
mkdir -p $(dirname ${TMP_CHECKS_PATH})

# add the header
echo "#ifndef __omx_checks_h__" >> ${TMP_CHECKS_PATH}
echo "#define __omx_checks_h__ 1" >> ${TMP_CHECKS_PATH}
echo "" >> ${TMP_CHECKS_PATH}

# what command line was used to generate with file
echo "/*" >> ${TMP_CHECKS_PATH}
echo " * ${CHECKS_DATE_PREFIX}"`date` >> ${TMP_CHECKS_PATH}
echo " * The configure line was: $ac_configure $ac_configure_args" >> ${TMP_CHECKS_PATH}
echo " * It checked kernel headers in ${LINUX_HDR}/include/" >> ${TMP_CHECKS_PATH}
echo " */" >> ${TMP_CHECKS_PATH}
echo "" >> ${TMP_CHECKS_PATH}

# remap_vmalloc_range and vmalloc_user appeared in 2.6.18
AC_MSG_CHECKING((in kernel headers) remap_vmalloc_range availability)
if grep "remap_vmalloc_range *(" ${LINUX_HDR}/include/linux/vmalloc.h > /dev/null ; then
  echo "#define OMX_HAVE_REMAP_VMALLOC_RANGE 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

# miscdevice pointed to a class_device before 2.6.20
AC_MSG_CHECKING((in kernel headers) device type in miscdevice)
if sed -ne '/^struct miscdevice/,/^};/p' ${LINUX_HDR}/include/linux/miscdevice.h \
   | grep "struct class_device" > /dev/null ; then
  echo "#define OMX_MISCDEV_HAVE_CLASS_DEVICE 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(struct class_device)
else
  AC_MSG_RESULT(struct device)
fi

# dev_base list replaced with for_each_netdev added in 2.6.22
# and modified for net namespaces in 2.6.24
AC_MSG_CHECKING((in kernel headers) for_each_netdev availability)
if grep "for_each_netdev *(" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null ; then
  if grep "for_each_netdev *(.*,.*)" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null ; then
    echo "#define OMX_HAVE_FOR_EACH_NETDEV 1" >> ${TMP_CHECKS_PATH}
    AC_MSG_RESULT(yes)
  else
    echo "#define OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS 1" >> ${TMP_CHECKS_PATH}
    AC_MSG_RESULT([yes, without namespaces])
  fi
else
  AC_MSG_RESULT(no)
fi

# dev_get_by_name got a namespace argument in 2.6.24
AC_MSG_CHECKING((in kernel headers) dev_get_by_name prototype)
if grep "struct net_device.*dev_get_by_name *(.*,.*)" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null ; then
  AC_MSG_RESULT(with namespace)
else  
  echo "#define OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(without namespace)
fi

# skb got mac/network/transport headers in 2.6.22
AC_MSG_CHECKING((in kernel headers) skb headers availability)
if grep "sk_buff_data_t.*mac_header;" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null ; then
  echo "#define OMX_HAVE_SKB_HEADERS 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

# skb_pad returned a new skb up to 2.6.17
AC_MSG_CHECKING((in kernel headers) skb_pad return type)
if grep "struct sk_buff *\*[[ 	]]*skb_pad(.*, .*);" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null ; then
  echo "#define OMX_SKB_PAD_RETURNS_NEW_SKB 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(struct sk_buff *)
else
  AC_MSG_RESULT(int)
fi

# uts namespace arrived in 2.6.19
AC_MSG_CHECKING((in kernel headers) UTS namespace availabilty)
if grep "uts_ns" ${LINUX_HDR}/include/linux/utsname.h > /dev/null ; then
  echo "#define OMX_HAVE_UTS_NAMESPACE 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

# dmaengine really usable when channel may be shared among clients, arrived in 2.6.23
AC_MSG_CHECKING((in kernel headers) whether DMA engine channels are shareable)
if test -e ${LINUX_HDR}/include/linux/dmaengine.h \
  && grep DMA_RESOURCE_AVAILABLE ${LINUX_HDR}/include/linux/dmaengine.h > /dev/null ; then
  echo "#define OMX_HAVE_SHAREABLE_DMA_CHANNELS 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

# mutexes appeared in 2.6.16
AC_MSG_CHECKING((in kernel headers) whether mutexes are available)
if test -e ${LINUX_HDR}/include/linux/mutex.h > /dev/null ; then
  echo "#define OMX_HAVE_MUTEX 1" >> ${TMP_CHECKS_PATH}
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

# add the footer
echo "" >> ${TMP_CHECKS_PATH}
echo "#endif /* __omx_checks_h__ */" >> ${TMP_CHECKS_PATH}

# install final file
if diff -q ${CHECKS_PATH} ${TMP_CHECKS_PATH} --ignore-matching-lines="${CHECKS_DATE_PREFIX}" >/dev/null 2>&1; then
  rm -f ${TMP_CHECKS_PATH}
  AC_MSG_NOTICE(${CHECKS_NAME} is unchanged)
else
  mv -f ${TMP_CHECKS_PATH} ${CHECKS_PATH}
  AC_MSG_NOTICE(creating kernel headers description in ${CHECKS_NAME})
fi

###############
# Other config

libopen_mx_dir="libopen-mx/"
libopen_mx_builddir="${libopen_mx_dir}build/"
libopen_mx_name="libopen-mx"
LIB_TARGETS=""

generate_lib_build() {
	dir=$1; shift
	name=$1; shift
	debug=$1; shift
	installsuffix=$1; shift

	mkdir -p ${libopen_mx_dir}${dir} || exit -1

	cat >${libopen_mx_dir}${dir}/Makefile <<EOF
omx_build_lib_debug = $debug
omx_build_lib_name = $name
libdirsuffix = ${installsuffix}
include ../Makefile.build
EOF
	test $? -eq 0 || exit -1

	AC_MSG_NOTICE(building the $name library in ${libopen_mx_dir}${dir})
	LIB_TARGETS="$LIB_TARGETS $dir"
}

AC_ARG_ENABLE(debug, [  --enable-debug		enable debugging features in the driver and main library],
	      enable_debug=$enableval, enable_debug=optional)
AC_MSG_CHECKING(if debug is enabled in the driver and main library)
AC_MSG_RESULT($enable_debug)
if test x$enable_debug = xyes; then
  # only build the debug lib lib
  AC_DEFINE(OMX_DRIVER_DEBUG, [1], [Define to enable debug features in the driver])
  generate_lib_build build debug-only 1 ""
else if test x$enable_debug = xno; then
  # only build the non-debug lib
  generate_lib_build build normal 0 ""
else
  # build both by default
  generate_lib_build build normal 0 ""
  generate_lib_build build-dbg debug 1 /debug
fi fi
AC_SUBST(LIB_TARGETS)

AC_ARG_ENABLE(self, [  --disable-self		disable communication to myself],
	      enable_self=$enableval, enable_self=yes)
AC_MSG_CHECKING(if self communication is enabled)
if test x$enable_self = xyes; then
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
  AC_DEFINE(OMX_DISABLE_SELF, [1], [Define to disable communication to myself])
fi

AC_ARG_ENABLE(shared, [  --disable-shared		disable shared communication between endpoints on the same node],
	      enable_shared=$enableval, enable_shared=yes)
AC_MSG_CHECKING(if shared communication is enabled)
if test x$enable_shared = xyes; then
  AC_MSG_RESULT(yes)
  AC_SUBST(OMX_DISABLE_SHARED, 0)
else
  AC_MSG_RESULT(no)
  AC_DEFINE(OMX_DISABLE_SHARED, [1], [Define to disable shared communication between endpoints on the same node])
  AC_SUBST(OMX_DISABLE_SHARED, 1)
fi

AC_ARG_ENABLE(valgrind, [  --enable-valgrind		enable Valgrind hooks in the library ],
	      enable_valgrind=$enableval, enable_valgrind=maybe)
AC_MSG_CHECKING(if valgrind hooks are enabled in the debug library)
if test x$enable_valgrind != xno; then
  AC_PREPROC_IFELSE([
#include <valgrind/memcheck.h>
#ifndef VALGRIND_MAKE_MEM_NOACCESS
#error VALGRIND_MAKE_MEM_NOACCESS not defined
#endif
    ], enable_valgrind=yes)
fi
if test x$enable_valgrind = xyes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE(OMX_VALGRIND_DEBUG, 1, [Enable memory allocation debugging with Valgrind])
else
  AC_MSG_RESULT(no)
fi

AC_ARG_WITH(peers-file, [  --with-peers-file=<path>		use argument as a static peers file],
	    with_peers_file=$withval, with_peers_file=/etc/open-mx/peers)
AC_MSG_NOTICE(using static peers file in $with_peers_file)
AC_SUBST(PEERS_FILE, $with_peers_file)

AC_ARG_ENABLE(mx, [  --disable-mx			disable MX API compatibility wrappers],
	      enable_mx=$enableval, enable_mx=yes)
AC_MSG_CHECKING(if MX API compatibility wrappers are enabled)
if test x$enable_mx = xyes; then
  AC_MSG_RESULT(yes)
  AC_DEFINE(OMX_MX_API_COMPAT, 1, [Define to enable MX API compatibility wrappers])
  AC_SUBST(OMX_MX_API_COMPAT, 1)
else
  AC_MSG_RESULT(no)
  AC_SUBST(OMX_MX_API_COMPAT, 0)
fi

AC_ARG_ENABLE(mx-wire, [  --disable-mx-wire		disable MX wire compatibility],
	      enable_mx_wire=$enableval, enable_mx_wire=yes)
AC_MSG_CHECKING(if MX wire compatibility is enabled)
if test x$enable_mx_wire = xyes; then
  AC_MSG_RESULT(yes)
  AC_DEFINE(OMX_MX_WIRE_COMPAT, 1, [Define to enable MX API compatibility wrappers])
else
  AC_MSG_RESULT(no)
fi

AC_ARG_WITH(ethertype, [  --with-ethertype=<value>		change Ethertype (default is 0x86DF)],
	    ethertype=$withval, ethertype=default)
AC_MSG_CHECKING(the Ethertype to use)
if test x$ethertype = xdefault; then
  AC_MSG_RESULT([default (0x86df)])
else
  AC_MSG_RESULT($ethertype)
  AC_DEFINE_UNQUOTED(ETH_P_OMX, $ethertype, [Define to change the Ethertype])
fi

AC_ARG_ENABLE(endian, [  --disable-endian		disable endian-independent networking],
	      enable_endian=$enableval, enable_endian=yes)
AC_MSG_CHECKING(if endian-independent networking is enabled)
if test x$enable_endian = xyes; then
  AC_MSG_RESULT(yes)
  AC_DEFINE(OMX_ENDIAN_COMPAT, 1, [Define to enable endian-independent networking])
else
  AC_MSG_RESULT(no)
fi

AC_ARG_WITH(device, [  --with-device=<path>:<group>:<mode>	change device node name, group and/or mode],
	    device_attrs=$withval, device_attrs="")
DEVICE_NAME=/dev/open-mx
device_name=$(echo $device_attrs | cut -d: -f1)
if ! test -z "$device_name"; then
  DEVICE_NAME=$device_name
  AC_DEFINE_UNQUOTED(OMX_DEVNAME, "$device_name", [Define to change the device node name])
fi
AC_SUBST(DEVICE_NAME)
DEVICE_GROUP=root
device_group=$(echo $device_attrs | cut -d: -f2)
if ! test -z "$device_group"; then
  DEVICE_GROUP=$device_group
fi
AC_SUBST(DEVICE_GROUP)
DEVICE_MODE=0666
device_mode=$(echo $device_attrs | cut -d: -f3)
if ! test -z "$device_mode"; then
  DEVICE_MODE=$device_mode
fi
AC_SUBST(DEVICE_MODE)

##########
# Globals

AC_SUBST(OPEN_MX_LIB_STATIC, ${libopen_mx_name}.a)
AC_SUBST(OPEN_MX_LIB_DYNAMIC, ${libopen_mx_name}.so)
AC_SUBST(OPEN_MX_LIB_STATIC_PATH, ${libopen_mx_builddir}/${libopen_mx_name}.a)
AC_SUBST(OPEN_MX_LIB_DYNAMIC_PATH, ${libopen_mx_builddir}/${libopen_mx_name}.so)

AC_CONFIG_HEADER(common/omx_config.h)
AC_OUTPUT(Makefile driver/linux/Makefile driver/linux/Kbuild
	  libopen-mx/Makefile libopen-mx/Makefile.build
	  tools/Makefile tests/Makefile tests/mx/Makefile)
