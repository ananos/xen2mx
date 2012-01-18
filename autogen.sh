#! /bin/sh

# Open-MX
# Copyright © inria 2007-2010 (see AUTHORS file)
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

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

version=`sed -n /AC_INIT\(/,/\)/p configure.ac | tr -d '\n\t' | cut -d, -f2`
if test x$VERSION != x; then
	echo "Updating configure.ac version..."
	sed /AC_INIT\(/,/\)/s/"$version"/"$VERSION"/g -i configure.ac
	version="$VERSION"
fi
echo "Updating COPYING version..."
sed -e 's/^Open-MX .*/Open-MX '${version}'/' -i COPYING
echo "Updating open-mx.spec version..."
sed -e 's/^Version: .*/Version: '${version}'/' -i open-mx.spec
echo "Updating dkms.conf version..."
sed -e 's/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"'${version}'\"/' -i dkms.conf

echo "Creating the build-aux directory if necessary..."
mkdir -p build-aux
echo "Creating the macro directory if necessary..."
mkdir -p m4
echo "Running aclocal..."
aclocal --force || exit 1
echo "Running autoheader..."
autoheader -f || exit 1
echo "Running libtoolize..."
libtoolize -cf || exit 1
echo "Running automake..."
automake -ac || exit 1
echo "Running autoconf..."
autoconf -f || exit 1

cd $ORIGDIR || exit $?
