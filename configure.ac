AC_INIT(configure.ac)
AC_PREREQ(2.53)
AC_CANONICAL_SYSTEM
AC_PREFIX_DEFAULT(/opt/mpoe)

AC_PROG_CPP
AC_PROG_CC

########################
# Kernel related config

LINUX_SRC="/lib/modules/"`uname -r`"/source"
LINUX_BUILD="/lib/modules/"`uname -r`"/build"
# If the user changes the source, use it as build by default
# If it changes the build, do not use it as the source by default
AC_ARG_WITH(linux,
	[  --with-linux=<dir>	location of the Linux kernel source],
	LINUX_SRC="$withval"; LINUX_BUILD="$withval")
AC_ARG_WITH(linux-build,
	[  --with-linux-build=<dir>	location of the Linux kernel build tree],
	LINUX_BUILD="$withval")
AC_SUBST(LINUX_SRC)
AC_SUBST(LINUX_BUILD)

# Find where the headers are (to avoid grepping at both places).
# Do not check for autoconf.h or version.h since these are in
# both the source and the build directory.
AC_MSG_CHECKING(include/linux/kernel.h)
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

# create the output file
CHECKS_NAME=driver/linux/omx_checks.h
CHECKS_PATH=`pwd`/${CHECKS_NAME}
TMP_CHECKS_PATH=${CHECKS_PATH}.tmp
rm -f ${TMP_CHECKS_PATH}
mkdir -p $(dirname ${TMP_CHECKS_PATH})

# add the header
echo "#ifndef __omx_checks_h__" >> ${TMP_CHECKS_PATH}
echo "#define __omx_checks_h__ 1" >> ${TMP_CHECKS_PATH}
echo "" >> ${TMP_CHECKS_PATH}

# what command line was used to generate with file
echo "/*" >> ${TMP_CHECKS_PATH}
echo " * This file has been generated with on "`date` >> ${TMP_CHECKS_PATH}
echo " * The configure line was: $ac_configure $ac_configure_args" >> ${TMP_CHECKS_PATH}
echo " * It checked kernel headers in ${LINUX_HDR}/include/" >> ${TMP_CHECKS_PATH}
echo " */" >> ${TMP_CHECKS_PATH}
echo "" >> ${TMP_CHECKS_PATH}

# skb_linearize had a gfp argument before 2.6.18
#AC_MSG_CHECKING(skb_linearize gfp argument)
#grep "skb_linearize *(.*, .* gfp)" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null \
#  && echo "#define MPOE_SKB_LINEARIZE_HAS_GFP 1" >> ${TMP_CHECKS_PATH} \
#  && AC_MSG_RESULT(yes) || AC_MSG_RESULT(no)

# netdev_alloc_skb deprecates dev_alloc_skb in 2.6.18
AC_MSG_CHECKING(netdev_alloc_skb availability)
grep "netdev_alloc_skb *(" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null \
  && echo "#define MPOE_HAVE_NETDEV_ALLOC_SKB 1" >> ${TMP_CHECKS_PATH} \
  && AC_MSG_RESULT(yes) || AC_MSG_RESULT(no)

# remap_vmalloc_range and vmalloc_user appeared in 2.6.18
AC_MSG_CHECKING(remap_vmalloc_range availability)
grep "remap_vmalloc_range *(" ${LINUX_HDR}/include/linux/vmalloc.h > /dev/null \
  && echo "#define MPOE_HAVE_REMAP_VMALLOC_RANGE 1" >> ${TMP_CHECKS_PATH} \
  && AC_MSG_RESULT(yes) || AC_MSG_RESULT(no)

# miscdevice pointed to a class_device before 2.6.20
AC_MSG_CHECKING(device type in miscdevice)
sed -ne '/^struct miscdevice/,/^};/p' ${LINUX_HDR}/include/linux/miscdevice.h \
 | grep "struct class_device" > /dev/null \
 && echo "#define MPOE_MISCDEV_HAVE_CLASS_DEVICE 1" >> ${TMP_CHECKS_PATH} \
 && AC_MSG_RESULT(struct class_device) || AC_MSG_RESULT(struct device)

# dev_base list replaced with for_each_netdev added in 2.6.22
AC_MSG_CHECKING(for_each_netdev availability)
grep "for_each_netdev *(" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null \
  && echo "#define MPOE_HAVE_FOR_EACH_NETDEV 1" >> ${TMP_CHECKS_PATH} \
  && AC_MSG_RESULT(yes) || AC_MSG_RESULT(no)

# skb got mac/network/transport headers in 2.6.22
AC_MSG_CHECKING(skb headers availability)
grep "sk_buff_data_t.*mac_header;" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null \
  && echo "#define MPOE_HAVE_SKB_HEADERS 1" >> ${TMP_CHECKS_PATH} \
  && AC_MSG_RESULT(yes) || AC_MSG_RESULT(no)

# skb_pad returned a new skb up to 2.6.17
AC_MSG_CHECKING(skb_pad return type)
grep "struct sk_buff *\*[[ 	]]*skb_pad(.*, .*);" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null \
  && echo "#define MPOE_SKB_PAD_RETURNS_NEW_SKB 1" >> ${TMP_CHECKS_PATH} \
  && AC_MSG_RESULT(struct sk_buff *) || AC_MSG_RESULT(int)

# uts namespace arrived in 2.6.19
AC_MSG_CHECKING(uts namespace availabilty)
grep "uts_ns" ${LINUX_HDR}/include/linux/utsname.h > /dev/null \
  && echo "#define MPOE_HAVE_UTS_NAMESPACE 1" >> ${TMP_CHECKS_PATH} \
  && AC_MSG_RESULT(yes) || AC_MSG_RESULT(no)

# add the footer
echo "" >> ${TMP_CHECKS_PATH}
echo "#endif /* __omx_checks_h__ */" >> ${TMP_CHECKS_PATH}

# install final file
if diff -q ${CHECKS_PATH} ${TMP_CHECKS_PATH} >/dev/null 2>&1; then
  rm -f ${TMP_CHECKS_PATH}
  AC_MSG_NOTICE(${CHECKS_NAME} is unchanged)
else
  mv -f ${TMP_CHECKS_PATH} ${CHECKS_PATH}
  AC_MSG_NOTICE(creating ${CHECKS_NAME})
fi

###############
# Other config

AC_ARG_ENABLE(debug, [  --enable-debug		enable debugging features],
	      enable_debug=$enableval, enable_debug=no)
AC_MSG_CHECKING(if debug is enabled)
if test x$enable_debug = xyes; then
  AC_MSG_RESULT(yes)
  AC_DEFINE(MPOE_DEBUG, [1], [Define to enable debug features])
else
  AC_MSG_RESULT(no)
fi

AC_ARG_ENABLE(valgrind, [ --enable-valgrind	enable Valgrind hooks in the library ],
	      enable_valgrind=$enableval, enable_valgrind=maybe)
AC_MSG_CHECKING(if valgrind hooks are enabled)
if test x$enable_valgrind != xno -a x$enable_debug = xyes; then
  AC_PREPROC_IFELSE([#include <valgrind/memcheck.h>], enable_valgrind=yes)
fi
if test x$enable_valgrind = xyes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE(MPOE_VALGRIND_DEBUG, 1, [Enable memory allocation debugging with Valgrind])
else
  AC_MSG_RESULT(no)
fi

##########
# Globals

AC_SUBST(LIBMPOE_A, libmpoe/libmpoe.a)

AC_CONFIG_HEADER(common/mpoe_config.h)
AC_OUTPUT(Makefile driver/linux/Makefile driver/linux/Kbuild libmpoe/Makefile tools/Makefile tests/Makefile)
