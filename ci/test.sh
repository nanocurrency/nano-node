#!/usr/bin/env bash

BUSYBOX_BASH=${BUSYBOX_BASH-0}

if [[ ${FLAVOR-_} == "_" ]]; then
    FLAVOR=""
fi

set -o nounset


# Alpine doesn't offer an xvfb
xvfb_run_() {
    INIT_DELAY_SEC=3

    Xvfb :2 -screen 0 1024x768x24 &
    xvfb_pid=$!
    sleep ${INIT_DELAY_SEC}
    DISPLAY=:2 timeout ${TIMEOUT_TIME_ARG} ${TIMEOUT_SEC-1200} $@
    res=${?}
    kill ${xvfb_pid}

    exit ${res}
}

run_tests() {
    build_dir=build_${FLAVOR}

    cd ./${build_dir}

    # when busybox pretends to be bash it needs different args
    #   for the timeout builtin
    if [[ "${BUSYBOX_BASH}" -eq 1 ]]; then
        TIMEOUT_TIME_ARG="-t"
    else
        TIMEOUT_TIME_ARG=""
    fi

    timeout ${TIMEOUT_TIME_ARG} ${TIMEOUT_SEC-1200} ./core_test
    core_test_res=${?}

    xvfb_run_ ./qt_test
    qt_test_res=${?}

    echo "Core Test return code: ${core_test_res}"
    echo "QT Test return code: ${qt_test_res}"
    exit $((${core_test_res} + ${qt_test_res}))
}

run_tests
