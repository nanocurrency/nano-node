#!/usr/bin/env bash

build_dir=${1-${PWD}}
if [[ ${TEST_USE_ROCKSDB-0} == 1 ]]; then
    TIMEOUT_DEFAULT=2400
else
    TIMEOUT_DEFAULT=1200
fi

BUSYBOX_BASH=${BUSYBOX_BASH-0}

if [[ ${FLAVOR-_} == "_" ]]; then
    FLAVOR=""
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    TIMEOUT_CMD=gtimeout
else
    TIMEOUT_CMD=timeout
fi

set -o nounset
set -o xtrace

# Alpine doesn't offer an xvfb
xvfb_run_()
{
    INIT_DELAY_SEC=3

    Xvfb :2 -screen 0 1024x768x24 &
    xvfb_pid=$!
    sleep ${INIT_DELAY_SEC}
    DISPLAY=:2 ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} ${TIMEOUT_DEFAULT} $@
    res=${?}
    kill ${xvfb_pid}

    return ${res}
}

run_tests()
{
    # when busybox pretends to be bash it needs different args for the timeout builtin
    #
    if [[ "${BUSYBOX_BASH}" -eq 1 ]]; then
        TIMEOUT_TIME_ARG="-t"
    else
        TIMEOUT_TIME_ARG=""
    fi

    ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} ${TIMEOUT_DEFAULT} ./core_test
    core_test_res=${?}

    ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} ${TIMEOUT_DEFAULT} ./rpc_test
    rpc_test_res=${?}

    xvfb_run_ ./qt_test
    qt_test_res=${?}

    (cd ../systest && export NANO_NODE_EXE=../build/nano_node && ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} 300 ./RUNALL)
    sys_test_res=${?}

    echo "Core Test return code: ${core_test_res}"
    echo "RPC  Test return code: ${rpc_test_res}"
    echo "QT Test return code: ${qt_test_res}"
    echo "Sys Test return code: ${sys_test_res}"

    if [[ ${core_test_res} != 0 || ${rpc_test_res} != 0 || ${qt_test_res} != 0 || ${sys_test_res} != 0 ]]; then
        return 1
    else
        return 0
    fi
}

cd ${build_dir}
run_tests
