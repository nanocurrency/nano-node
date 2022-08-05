#!/bin/bash

qt_dir=${1}
ci_version_pre_release="OFF"
if [[ -n "${CI_VERSION_PRE_RELEASE}" ]]; then
    ci_version_pre_release="$CI_VERSION_PRE_RELEASE"
fi

set -o errexit
set -o nounset
set -o xtrace
OS=$(uname)

mkdir build
pushd build
CONFIGURATION="Release"

if [[ "${BETA:-0}" -eq 1 ]]; then
    NETWORK_CFG="beta"
    CONFIGURATION="RelWithDebInfo"
elif [[ "${TEST:-0}" -eq 1 ]]; then
    NETWORK_CFG="test"
else
    NETWORK_CFG="live"
fi

cmake \
-G'Unix Makefiles' \
-DACTIVE_NETWORK=nano_${NETWORK_CFG}_network \
-DNANO_POW_SERVER=ON \
-DNANO_GUI=ON \
-DPORTABLE=1 \
-DCMAKE_BUILD_TYPE=${CONFIGURATION} \
-DCMAKE_VERBOSE_MAKEFILE=ON \
-DBOOST_ROOT=/tmp/boost/ \
-DNANO_SHARED_BOOST=ON \
-DQt5_DIR=${qt_dir} \
-DCI_BUILD=true \
-DCI_VERSION_PRE_RELEASE="${ci_version_pre_release}" \
..

if [[ "$OS" == 'Linux' ]]; then
    cmake --build ${PWD} --target package --config ${CONFIGURATION} -- -j$(nproc)
else
    sudo cmake --build ${PWD} --target package --config ${CONFIGURATION} -- -j2
fi

popd
