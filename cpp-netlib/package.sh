#!/bin/sh
#
# Copyright 2012 Dean Michael Berris <dberris@google.com>
# Copyright 2012 Google, Inc.
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#

VERSION="$1"
if [ "$VERSION" = "" ]; then
  VERSION="`git log --format=oneline | awk '{print $1}' | head -1`"
fi

# Break down the version in form "M.m.p" into individual variables.
BOOST_NETLIB_VERSION_MAJOR=`echo $VERSION | sed 's/[\._\-]/ /g' | awk '{print $1}'`
BOOST_NETLIB_VERSION_MINOR=`echo $VERSION | sed 's/[\._\-]/ /g' | awk '{print $2}'`
BOOST_NETLIB_VERSION_INCREMENT=`echo $VERSION | sed 's/[\._\-]/ /g' | awk '{print $3}'`

echo $BOOST_NETLIB_VERSION_MAJOR
echo $BOOST_NETLIB_VERSION_MINOR
echo $BOOST_NETLIB_VERSION_INCREMENT

# Then update the version.
sed -i '' 's/BOOST_NETLIB_VERSION_MAJOR [0-9]*/BOOST_NETLIB_VERSION_MAJOR '$BOOST_NETLIB_VERSION_MAJOR'/g' boost/network/version.hpp
sed -i '' 's/BOOST_NETLIB_VERSION_MINOR [0-9]*/BOOST_NETLIB_VERSION_MINOR '$BOOST_NETLIB_VERSION_MINOR'/g' boost/network/version.hpp
sed -i '' 's/BOOST_NETLIB_VERSION_INCREMENT [0-9]*/BOOST_NETLIB_VERSION_INCREMENT '$BOOST_NETLIB_VERSION_INCREMENT'/g' boost/network/version.hpp

# Show the diff
git diff boost/network/version.hpp

# Commit the change
git add boost/network/version.hpp
git commit -m"Bumping release number to $VERSION"

TAG="cpp-netlib-$VERSION"
git tag $TAG
echo "Tagged $TAG."

git archive --prefix=cpp-netlib-$VERSION/ --format=zip $TAG >cpp-netlib-$VERSION.zip
git archive --prefix=cpp-netlib-$VERSION/ --format=tar $TAG | gzip >cpp-netlib-$VERSION.tar.gz
git archive --prefix=cpp-netlib-$VERSION/ --format=tar $TAG | bzip2 >cpp-netlib-$VERSION.tar.bz2
echo "Packaged $TAG."
