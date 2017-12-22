#!/usr/bin/env bash

DISTRO_CFG=""
if [[ "$OSTYPE" == "linux-gnu" ]]; then
    CPACK_TYPE="TBZ2"
    distro=$(lsb_release -i -s)
    DISTRO_CFG="-DRAIBLOCKS_DISTRO_NAME=${distro}"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CPACK_TYPE="DragNDrop"
elif [[ "$OSTYPE" == "cygwin" ]]; then
    CPACK_TYPE="TBZ2"
elif [[ "$OSTYPE" == "msys" ]]; then
    CPACK_TYPE="NSIS" #?
elif [[ "$OSTYPE" == "win32" ]]; then
    CPACK_TYPE="NSIS"
elif [[ "$OSTYPE" == "freebsd"* ]]; then
    CPACK_TYPE="TBZ2"
    DISTRO_CFG="-DRAIBLOCKS_DISTRO_NAME='freebsd'"
else
    CPACK_TYPE="TBZ2"
fi

if [[ ${NO_SIMD} -eq 1 ]]; then
    NOSIMD_CFG="-DRAIBLOCKS_SIMD_OPTIMIZATIONS=OFF"
    CRYPTOPP_CFG="-DCRYPTOPP_CUSTOM=ON"
else
    NOSIMD_CFG=""
    CRYPTOPP_CFG=""
fi

if [[ ${ASAN_INT} -eq 1 ]]; then
    SANITIZERS="-DRAIBLOCKS_ASAN_INT=ON"
elif [[ ${ASAN} -eq 1 ]]; then
    SANITIZERS="-DRAIBLOCKS_ASAN=ON"
elif [[ ${TSAN} -eq 1 ]]; then
    SANITIZERS="-DRAIBLOCKS_TSAN=ON"
else
    SANITIZERS=""
fi

if [[ ${BOOST_ROOT} -ne "" ]]; then
    BOOST_CFG="-DBOOST_ROOT='${BOOST_ROOT}'"
else
    BOOST_CFG=""
fi

BUSYBOX_BASH=${BUSYBOX_BASH-0}
if [[ ${FLAVOR-_} == "_" ]]; then
    FLAVOR=""
fi

set -o nounset

run_build() {
    build_dir=build_${FLAVOR}

    mkdir ${build_dir}
    cd ${build_dir}
    cmake -GNinja \
       -DRAIBLOCKS_TEST=ON \
       -DRAIBLOCKS_GUI=ON \
       -DCMAKE_BUILD_TYPE=Debug \
       -DCMAKE_VERBOSE_MAKEFILE=ON \
       -DCMAKE_INSTALL_PREFIX="../install" \
       ${CRYPTOPP_CFG} \
       ${DISTRO_CFG} \
       ${NOSIMD_CFG} \
       ${BOOST_CFG} \
       ${SANITIZERS} \
       ..

    cmake --build ${PWD} -- -v
    cmake --build ${PWD} -- install -v
    cpack -G ${CPACK_TYPE} ${PWD}
}

run_build
