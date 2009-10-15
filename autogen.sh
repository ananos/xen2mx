#! /bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

echo "Running autoconf..."
autoconf || exit 1
echo "Running autoheader..."
autoheader || exit 1

cd $ORIGDIR || exit $?
