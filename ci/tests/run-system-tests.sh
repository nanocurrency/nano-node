#!/bin/bash
set -euo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

BUILD_DIR=${1-${PWD}}

export NANO_NODE_EXE=${BUILD_DIR}/nano_node$(get_exec_extension)
cd ../systest && ./RUNALL