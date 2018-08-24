#!/usr/bin/env bash

build_dir=${1-${PWD}}
TIMEOUT_DEFAULT=120

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
xvfb_run_() {
    INIT_DELAY_SEC=3

    Xvfb :2 -screen 0 1024x768x24 &
    xvfb_pid=$!
    sleep ${INIT_DELAY_SEC}
    DISPLAY=:2 ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} ${TIMEOUT_SEC-${TIMEOUT_DEFAULT}} $@
    res=${?}
    kill ${xvfb_pid}

    return ${res}
}

run_tests() {
    # when busybox pretends to be bash it needs different args
    #   for the timeout builtin
    if [[ "${BUSYBOX_BASH}" -eq 1 ]]; then
        TIMEOUT_TIME_ARG="-t"
    else
        TIMEOUT_TIME_ARG=""
    fi

    ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} ${TIMEOUT_SEC-${TIMEOUT_DEFAULT}} ./core_test
    core_test_res=${?}

    xvfb_run_ ./qt_test
    qt_test_res=${?}

    ${TIMEOUT_CMD} ${TIMEOUT_TIME_ARG} ${TIMEOUT_SEC-${TIMEOUT_DEFAULT}} ./load_test ./bananode -s 150
    load_test_res=${?}

    echo "Core Test return code: ${core_test_res}"
    echo "QT Test return code: ${qt_test_res}"
    echo "Load Test return code: ${load_test_res}"
    return ${core_test_res}
}

cd ${build_dir}
run_tests
