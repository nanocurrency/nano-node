#!/usr/bin/env bash

BOOST_BASENAME=boost_1_66_0
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}
BOOST_URL=http://sourceforge.net/projects/boost/files/boost/1.66.0/${BOOST_BASENAME}.tar.gz/download

wget -O ${BOOST_BASENAME}.tar.gz ${BOOST_URL}
tar xf ${BOOST_BASENAME}.tar.gz
cd ${BOOST_BASENAME}
./bootstrap.sh
./b2 --prefix=${BOOST_ROOT} link=static install
cd ..
rm -rf ${BOOST_BASENAME}
rm -f ${BOOST_BASENAME}.tar.gz
mkdir -p app
