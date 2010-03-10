# PUSH_BUILDENV()
# ---------------
# Push the current build environment
AC_DEFUN([PUSH_BUILDENV],
[
__CPPFLAGS_save="$CPPFLAGS"
__CFLAGS_save="$CFLAGS"
__LDFLAGS_save="$LDFLAGS"
])


# POP_BUILDENV()
# --------------
# Pop the precedently pushed build environment
AC_DEFUN([POP_BUILDENV],
[
CPPFLAGS="$__CPPFLAGS_save"
CFLAGS="$__CFLAGS_save"
LDFLAGS="$__LDFLAGS_save"
])


# GET_BUILD_STRING(VAR)
# ---------------------
# Store the build string in VAR
AC_DEFUN([GET_BUILD_STRING],
[
$1="`id -run 2>/dev/null`@`uname -n`:`cd ${srcdir} && pwd` `date`"
])


# OMX_ENABLE(ARGNAME, SHELLVAR, DESC, MSG) 
# ----------------------------------------
# Convenience wrapper over AC_ARG_ENABLE with the option deactivated by default
AC_DEFUN([OMX_ENABLE],
[
AC_MSG_CHECKING($4)
AC_ARG_ENABLE($1, AC_HELP_STRING(--enable-$1, $3), $2=$enableval, $2=no)

if test x$$2 = xyes ; then
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi
])


# OMX_DISABLE(ARGNAME, SHELLVAR, DESC, MSG) 
# -----------------------------------------
# Convenience wrapper over AC_ARG_ENABLE with the option activated by default
AC_DEFUN([OMX_DISABLE],
[
AC_MSG_CHECKING($4)
AC_ARG_ENABLE($1, AC_HELP_STRING(--disable-$1, $3), $2=$enableval, $2=yes)

if test x$$2 = xyes ; then
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi
])


# OMX_DEFINE(NAME, VALUE, DESC, TEST)
# -----------------------------------
# Convenience wrapper over AC_DEFINE
AC_DEFUN([OMX_DEFINE],
[
if $4 ; then
  AC_DEFINE_UNQUOTED($1, $2, $3)
fi
])


# OMX_WITH(ARGNAME, ARGSTRING, SHELLVAR, DESC, MSG, [DEFVAL])
# -------------------------------------------------
# Convenience wrapper over AC_ARG_WITH
AC_DEFUN([OMX_WITH],
[
AC_ARG_WITH($1, AC_HELP_STRING(--with-$1=<$2>, $4), $3=$withval, $3=$6)
AC_MSG_NOTICE($5)
])


# OMX_WITH_COND(ARGNAME, ARGSTRING, SHELLVAR, DESC, MSG, MSG_CONF, [DEFVAL])
# --------------------------------------------------------------------------
# The same as AC_ARG_WITH but the message displaying can be controlled by a condition
AC_DEFUN([OMX_WITH_COND],
[
AC_ARG_WITH($1, AC_HELP_STRING(--with-$1=<$2>, $4), $3=$withval, $3=$7)
if $6 ; then
    AC_MSG_NOTICE($5)
fi
])


# OMX_PROG_KCC([COMPILER])
# -----------------------
# Check whether the request compiler used to build kernel modules exists
AC_DEFUN([OMX_PROG_KCC],
[
AC_MSG_CHECKING(which compiler is used to build kernel modules)
if ! test -z $1 ; then
  AC_MSG_RESULT($1)

  AC_CHECK_PROGS(__KCC, $1)

  if test -z $__KCC ; then
     AC_MSG_ERROR(no compiler has been found to build kernel modules)
  fi
else
  AC_MSG_RESULT($CC)
fi
])


# OMX_FIND_KERNEL_HEADERS(VAR, PATHS)
# -----------------------------------
# Scan the directories in PATHS for one containing kernel headers and
# stores the result in VAR
AC_DEFUN([OMX_FIND_KERNEL_HEADERS],
[
AC_MSG_CHECKING(for kernel.h kernel header)
for dir in $2 ; do
  if test -f $dir/include/linux/kernel.h ; then
    $1=$dir
    AC_MSG_RESULT($dir)
    break
  fi
done

if test -z $$1 ; then
   AC_MSG_RESULT(not found in $2)
   AC_MSG_ERROR(no kernel header found)
fi
])


# OMX_FIND_KERNEL_AUTOCONF_HEADER(VAR, PATHS)
# -------------------------------------------
# Look for the kernel autoconf header in the PATH lists and stores the first successful
# path in VAR
AC_DEFUN([OMX_FIND_KERNEL_AUTOCONF_HEADER],
[
AC_MSG_CHECKING(for autoconf.h kernel header)
for dir in $2 ; do
    if test -f $dir/include/linux/autoconf.h -o -f $dir/include/generated/autoconf.h ; then
       $1=$dir
       AC_MSG_RESULT($dir)
       break
    fi
done

if test -z $$1 ; then
   AC_MSG_RESULT(not found in $2)
   AC_MSG_ERROR(no kernel autoconf header found)
fi
])


# OMX_CHECK_KBUILD_MAKEFILE(BUILD_TREE)
# -------------------------------------
# Check for the Kbuild Makefile in BUILD_TREE
AC_DEFUN([OMX_CHECK_KBUILD_MAKEFILE],
[
AC_MSG_CHECKING(for the Kbuild Makefile)
if test -f $1/Makefile ; then
   AC_MSG_RESULT(found in $1)
else
   AC_MSG_RESULT(not found)
   AC_MSG_ERROR(no Kbuild Makefile found)
fi
])


# OMX_GET_KERNEL_RELEASE(VAR, BUILD_TREE)
# ---------------------------------------
# Store in VAR the real Linux kernel release pointed by BUILD_TREE
AC_DEFUN([OMX_GET_KERNEL_RELEASE],
[
$1=$(make kernelrelease -C $2 | grep ^2.6.)
])


# OMX_GET_KERNEL_GCC_VERSION(VAR)
# -------------------------------
# Store in VAR the version of GGC used to compile the kernel
AC_DEFUN([OMX_GET_KERNEL_GCC_VERSION],
[
$1=$(sed -e 's/.*gcc version \(.\..\).*/\1/' /proc/version)
])


# OMX_GET_GCC_VERSION(VAR, GCCPATH)
# ---------------------------------
# Assuming GCCPATH points to a GCC compiler, store in VAR the version of this compiler
AC_DEFUN([OMX_GET_GCC_VERSION],
[
$1=$($2 --version 2>/dev/null | sed -nr -e 's/.* ([[0-9]]+\.[[0-9]]+).*/\1/p' | head -1)
])


# OMX_SYMLINK_DRIVER_SOURCES()
# ----------------------------
# Symlink driver sources into the build tree if needed
AC_DEFUN([OMX_SYMLINK_DRIVER_SOURCES],
[
mkdir -p driver/linux

if test ! "$srcdir" -ef . ; then
   # Symlink kernel sources into the build tree if needed
   AC_MSG_NOTICE(creating symlinks to kernel sources in driver/linux/ ...)

   for file in $srcdir/driver/linux/*.[[ch]] ; do
       filename=$(basename $file)
       AC_CONFIG_LINKS(driver/linux/$filename:driver/linux/$filename)
   done
fi
])


# OMX_FIND_KBUILD_CFLAGS_ARG(VAR, BUILD_TREE)
# -------------------------------------------
# Get the type of CFLAGS directive supported by the kernel located in BUILD_TREE
# and store it in VAR
AC_DEFUN([OMX_FIND_KBUILD_CFLAGS_ARG],
[
AC_MSG_CHECKING(whether kernel build supports ccflags-y)

if test -d $2/scripts && grep ccflags-y -r $2/scripts > /dev/null ; then
   AC_MSG_RESULT(yes)
   $1=ccflags-y
else
   AC_MSG_RESULT(no, reverting to EXTRA_CFLAGS)
   $1=EXTRA_CFLAGS
fi
])


# OMX_CHECK_VALGRIND_HEADERS()
# ----------------------------
# Check for Valgrind headers
AC_DEFUN([OMX_CHECK_VALGRIND_HEADERS],
[
AC_MSG_CHECKING(whether Valgrind headers are available)

AC_PREPROC_IFELSE(
[
#include <valgrind/memcheck.h>
#ifndef VALGRIND_MAKE_MEM_NOACCESS
#error  VALGRIND_MAKE_MEM_NOACCESS not defined
#endif
],
__valgrind_available=yes)

if test x$__valgrind_available = xyes ; then
   AC_MSG_RESULT(yes)
else
   AC_MSG_RESULT(no)
   AC_MSG_ERROR(cannot used Valgrind hooks without Valgrind headers)
fi
])


# OMX_CHECK_KERNEL_HEADERS(BUILD_TREE, HEADER_TREE, LINUX_RELEASE)
# --------------------------
# Check whether Open-MX is compliant with the Linux kernel internal API
AC_DEFUN([OMX_CHECK_KERNEL_HEADERS],
[
AC_MSG_NOTICE(checking kernel headers...)
if ! $srcdir/driver/linux/check_kernel_headers.sh --force ./driver/linux/omx_checks.h "$1" "$2" "$3" ; then
   AC_MSG_ERROR(Open-MX is not compliant with the Linux kernel internal API)
fi
])
