#!/bin/bash

qt_dir=${1}
src_dir=${2}

set -o errexit
set -o nounset
set -o xtrace
OS=`uname`

mkdir build
pushd build

if [[ ${ASAN_INT-0} -eq 1 ]]; then
    SANITIZERS="-DBANANO_ASAN_INT=ON"
elif [[ ${ASAN-0} -eq 1 ]]; then
    SANITIZERS="-DBANANO_ASAN=ON"
elif [[ ${TSAN-0} -eq 1 ]]; then
    SANITIZERS="-DBANANO_TSAN=ON"
else
    SANITIZERS=""
fi

cmake \
    -G'Unix Makefiles' \
    -DACTIVE_NETWORK=banano_test_network \
    -DBANANO_TEST=ON \
    -DBANANO_GUI=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT=/usr/local \
    -DQt5_DIR=${qt_dir} \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    ${SANITIZERS} \
    ..


if [[ "$OS" == 'Linux' ]]; then
    cmake --build ${PWD} -- -j2
else
    sudo cmake --build ${PWD} -- -j2
fi

popd

pushd load-tester
cargo build --release
popd
cp ./load-tester/target/release/banano-load-tester ./build/load_test

./ci/test.sh ./build
