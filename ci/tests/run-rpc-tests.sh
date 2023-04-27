#!/bin/bash
set -euo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

BUILD_DIR=${1-${PWD}}

${BUILD_DIR}/rpc_test$(get_exec_extension)