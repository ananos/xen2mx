#! /bin/sh

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

echo "Creating the build-aux directory if necessary..."
mkdir -p build-aux
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
