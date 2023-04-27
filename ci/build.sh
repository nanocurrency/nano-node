#!/bin/bash
set -euox pipefail

NODE_SRC=${1:-${PWD}}

OS=$(uname)
BUILD_TYPE=${NANO_BUILD_TYPE:-Debug}
BUILD_TARGET=build_tests

if [[ "${RELEASE:-false}" == "true" ]]; then
    BUILD_TYPE="RelWithDebInfo"
fi

if [[ ${NANO_ASAN_INT:-0} -eq 1 ]]; then
    SANITIZERS="-DNANO_ASAN_INT=ON"
fi
if [[ ${NANO_ASAN:-0} -eq 1 ]]; then
    SANITIZERS="-DNANO_ASAN=ON"
fi
if [[ ${NANO_TSAN:-0} -eq 1 ]]; then
    SANITIZERS="-DNANO_TSAN=ON"
fi
if [[ ${NANO_COVERAGE:-0} -eq 1 ]]; then
    SANITIZERS="-DCOVERAGE=ON"
fi

if [[ "$OS" == 'Linux' ]]; then
    BACKTRACE="-DNANO_STACKTRACE_BACKTRACE=ON"

    if [[ "$COMPILER" == 'clang' ]]; then
        BACKTRACE="${BACKTRACE} -DNANO_BACKTRACE_INCLUDE=</tmp/backtrace.h>"
    fi
else
    BACKTRACE=""
fi

mkdir -p build
pushd build

cmake \
-DACTIVE_NETWORK=nano_dev_network \
-DNANO_TEST=ON \
-DNANO_GUI=ON \
-DPORTABLE=ON \
-DNANO_WARN_TO_ERR=ON \
-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
-DQt5_DIR=${NANO_QT_DIR:-} \
${BACKTRACE:-} \
${SANITIZERS:-} \
${NODE_SRC}

number_of_processors() {
    case "$(uname -s)" in
        Linux*)     
            nproc
            ;;
        Darwin*)    
            sysctl -n hw.ncpu
            ;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo "${NUMBER_OF_PROCESSORS}"
            ;;
        *)
            echo "Unknown OS"
            exit 1
            ;;
    esac
}

parallel_build_flag() {
    case "$(uname -s)" in
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo "-- -m"
            ;;
        *)
            echo "--parallel $(number_of_processors)"
            ;;
    esac
}

cmake --build ${PWD} --target ${BUILD_TARGET} $(parallel_build_flag)

popd