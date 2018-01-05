#!/bin/bash
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
    -DQt5_DIR=$1 \
    ..

make -j2 rai_node
make -j2 core_test

if [[ "$OS" == 'Linux' ]]; then
    make -j2 rai_wallet
else
    sudo make -j2 rai_wallet
fi

# Exclude flaky or stalling tests.
#./core_test --gtest_filter="-gap_cache.gap_bootstrap:bulk_pull.get_next_on_open:system.system_genesis"

popd
