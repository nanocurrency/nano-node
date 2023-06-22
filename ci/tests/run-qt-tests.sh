#!/bin/bash
set -euo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

BUILD_DIR=${1-${PWD}}

# Alpine doesn't offer an xvfb
xvfb_run_()
{
    INIT_DELAY_SEC=3

    Xvfb :2 -screen 0 1024x768x24 &
    xvfb_pid=$!
    sleep ${INIT_DELAY_SEC}
    DISPLAY=:2 $@
    res=${?}
    kill ${xvfb_pid}

    return ${res}
}

xvfb_run_ ${BUILD_DIR}/qt_test$(get_exec_extension)