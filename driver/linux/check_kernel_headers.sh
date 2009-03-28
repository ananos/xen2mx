# Open-MX
# Copyright © INRIA 2007-2009 (see AUTHORS file)
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

FORCE=0

if test $# -ge 1 && test "$1" = "--force" ; then
  FORCE=1
  shift
fi

if test $# -lt 4 ; then
  echo "Options:"
  echo "  --force	Check again even if the arguments did not change"
  echo "Need 4 command line arguments:"
  echo "  - header checks output file"
  echo "  - kernel build path"
  echo "  - kernel headers path"
  echo "  - kernel release"
  exit -1
fi

CHECKS_NAME="$1"
LINUX_BUILD="$2"
LINUX_HDR="$3"
LINUX_RELEASE="$4"

CONFIG_LINE="Ran with BUILD=\"$LINUX_BUILD\" HDR=\"$LINUX_HDR\" RELEASE=\"$LINUX_RELEASE\""
if test "$FORCE" != 1 && grep "$CONFIG_LINE" "$CHECKS_NAME" >/dev/null 2>&1; then
  # no need to rerun
  exit 0
fi

# create the output file
CHECKS_DATE_PREFIX="This file has been first generated on "
TMP_CHECKS_NAME=${CHECKS_NAME}.tmp
rm -f ${TMP_CHECKS_NAME}

# add the header
echo "#ifndef __omx_checks_h__" >> ${TMP_CHECKS_NAME}
echo "#define __omx_checks_h__ 1" >> ${TMP_CHECKS_NAME}
echo "" >> ${TMP_CHECKS_NAME}

# what command line was used to generate with file
echo "/*" >> ${TMP_CHECKS_NAME}
echo " * ${CHECKS_DATE_PREFIX}"`date` >> ${TMP_CHECKS_NAME}
echo " * ${CONFIG_LINE}" >> ${TMP_CHECKS_NAME}
echo " * It checked kernel headers in ${LINUX_HDR}/include/" >> ${TMP_CHECKS_NAME}
echo " */" >> ${TMP_CHECKS_NAME}
echo "" >> ${TMP_CHECKS_NAME}

# SANITY CHECKS

# vm_insert_page appeared in 2.6.15
echo -n "  checking (in kernel headers) vm_insert_page availability ... "
if grep vm_insert_page ${LINUX_HDR}/include/linux/mm.h > /dev/null ; then
  echo yes
else
  echo "no, this kernel isn't supported"
  exit -1
fi

# setup_timer appeared in 2.6.15
echo -n "  checking (in kernel headers) setup_timer availability ... "
if grep setup_timer ${LINUX_HDR}/include/linux/timer.h > /dev/null ; then
  echo yes
else
  echo "no, this kernel isn't supported"
  exit -1
fi

# kzalloc appeared in 2.6.14
echo -n "  checking (in kernel headers) kzalloc availability ... "
if grep kzalloc ${LINUX_HDR}/include/linux/slab*.h > /dev/null ; then
  echo yes
else
  echo "no, this kernel isn't supported"
  exit -1
fi

# API WORKAROUNDS

# vmalloc_user appeared in 2.6.18 but was broken until 2.6.19
echo -n "  checking (in kernel headers) vmalloc_user availability... "
if grep "vmalloc_user *(" ${LINUX_HDR}/include/linux/vmalloc.h > /dev/null ; then
  if grep "LINUX_VERSION_CODE 132626" ${LINUX_HDR}/include/linux/version.h > /dev/null ; then
    echo broken, ignoring
  else
    echo "#define OMX_HAVE_VMALLOC_USER 1" >> ${TMP_CHECKS_NAME}
    echo yes
  fi
else
  echo no
fi

# remap_vmalloc_range appeared in 2.6.18
echo -n "  checking (in kernel headers) remap_vmalloc_range availability ... "
if grep "remap_vmalloc_range *(" ${LINUX_HDR}/include/linux/vmalloc.h > /dev/null ; then
  echo "#define OMX_HAVE_REMAP_VMALLOC_RANGE 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# dev_base list replaced with for_each_netdev added in 2.6.22
# and modified for net namespaces in 2.6.24
echo -n "  checking (in kernel headers) for_each_netdev availability ... "
if grep "for_each_netdev *(" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null ; then
  if grep "for_each_netdev *(.*,.*)" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null ; then
    echo "#define OMX_HAVE_FOR_EACH_NETDEV 1" >> ${TMP_CHECKS_NAME}
    echo yes
  else
    echo "#define OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS 1" >> ${TMP_CHECKS_NAME}
    echo "yes, without namespaces"
  fi
else
  echo no
fi

# dev_get_by_name got a namespace argument in 2.6.24
echo -n "  checking (in kernel headers) dev_get_by_name prototype ... "
if grep "struct net_device.*dev_get_by_name *(.*,.*)" ${LINUX_HDR}/include/linux/netdevice.h > /dev/null ; then
  echo "with namespace"
else
  echo "#define OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS 1" >> ${TMP_CHECKS_NAME}
  echo "without namespace"
fi

# skb got mac/network/transport headers in 2.6.22
echo -n "  checking (in kernel headers) skb headers availability ... "
if grep "sk_buff_data_t.*mac_header;" ${LINUX_HDR}/include/linux/skbuff.h > /dev/null ; then
  echo "#define OMX_HAVE_SKB_HEADERS 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# task_struct nsproxy arrived in 2.6.19
echo -n "  checking (in kernel headers) nsproxy presence in task_struct ... "
if sed -ne '/^struct task_struct/,/^};/p' ${LINUX_HDR}/include/linux/sched.h \
  | grep "struct nsproxy" > /dev/null ; then
  echo "#define OMX_HAVE_TASK_STRUCT_NSPROXY 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# mutexes appeared in 2.6.16
echo -n "  checking (in kernel headers) whether mutexes are available ... "
if test -e ${LINUX_HDR}/include/linux/mutex.h > /dev/null ; then
  echo "#define OMX_HAVE_MUTEX 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# dev_to_node appeared in 2.6.20
echo -n "  checking (in kernel headers) whether dev_to_node is available ... "
if grep dev_to_node ${LINUX_HDR}/include/linux/device.h > /dev/null ; then
  echo "#define OMX_HAVE_DEV_TO_NODE 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# net_device.dev appeared in 2.6.21
echo -n "  checking (in kernel headers) device type in net_device ... "
if sed -ne '/^struct net_device/,/^};/p' ${LINUX_HDR}/include/linux/netdevice.h \
  | grep "struct class_device" > /dev/null ; then
  echo "#define OMX_HAVE_NETDEVICE_CLASS_DEVICE 1" >> ${TMP_CHECKS_NAME}
  echo "struct class_device"
else
  echo "struct device"
fi

# work_struct lost its data field in 2.6.20
echo -n "  checking (in kernel headers) whether workstruct contains a data field ... "
if sed -ne '/^struct work_struct {/,/^};/p' ${LINUX_HDR}/include/linux/workqueue.h \
  | grep "void \*data;" > /dev/null ; then
  echo "#define OMX_HAVE_WORK_STRUCT_DATA 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# dmaengine API reworked in 2.6.29
echo -n "  checking (in kernel headers) the dmaengine interface ... "
if test -e ${LINUX_HDR}/include/linux/dmaengine.h > /dev/null ; then
  if grep dmaengine_get ${LINUX_HDR}/include/linux/dmaengine.h > /dev/null ; then
    echo "#define OMX_HAVE_DMA_ENGINE_API 1" >> ${TMP_CHECKS_NAME}
    echo yes
  else
    echo "#define OMX_HAVE_OLD_DMA_ENGINE_API 1" >> ${TMP_CHECKS_NAME}
    echo "yes, the old one"
  fi
else
  echo no
fi

# dev_name added in 2.6.26 and bus_id removed in 2.6.23
echo -n "  checking (in kernel headers) whether dev_name is available ..."
if grep -w "dev_name" ${LINUX_HDR}/include/linux/device.h > /dev/null ; then
  echo "#define OMX_HAVE_DEV_NAME 1" >> ${TMP_CHECKS_NAME}
  echo yes
else
  echo no
fi

# add the footer
echo "" >> ${TMP_CHECKS_NAME}
echo "#endif /* __omx_checks_h__ */" >> ${TMP_CHECKS_NAME}

# install final file
if diff -q ${CHECKS_NAME} ${TMP_CHECKS_NAME} --ignore-matching-lines="${CHECKS_DATE_PREFIX}" >/dev/null 2>&1; then
  echo "  driver/linux/${CHECKS_NAME} is unchanged"
  rm -f ${TMP_CHECKS_NAME}
else
  echo "  creating driver/linux/${CHECKS_NAME}"
  mv -f ${TMP_CHECKS_NAME} ${CHECKS_NAME}
fi
