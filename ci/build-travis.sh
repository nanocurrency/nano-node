#!/bin/bash

qt_dir=${1}
src_dir=${2}

set -e
OS=`uname`

mkdir build
pushd build

cmake \
    -DACTIVE_NETWORK=rai_test_network \
    -DRAIBLOCKS_TEST=ON \
    -DRAIBLOCKS_GUI=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT=/usr/local \
    -DQt5_DIR=${qt_dir} \
    ..


if [[ "$OS" == 'Linux' ]]; then
    make -j2
else
    sudo make -j2
fi

popd

if [[ "$OSTYPE" == "darwin"* ]]; then
    TRUE_CMD=gtrue
else
    TRUE_CMD=true
fi
./ci/test.sh ./build || ${TRUE}
