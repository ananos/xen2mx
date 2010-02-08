#! /bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

echo "Running autoconf..."
autoconf || exit 1
echo "Running autoheader..."
autoheader || exit 1
echo "Updating COPYING version..."
version=`sed -n /AC_INIT\(/,/\)/p configure.ac | tr -d '\n ' | cut -d, -f2`
sed -e 's/^Open-MX: .*/Open-MX'${version}'/' -i COPYING
echo "Updating open-mx.spec version..."
sed -e 's/^Version: .*/Version: '${version}'/' -i open-mx.spec

cd $ORIGDIR || exit $?
