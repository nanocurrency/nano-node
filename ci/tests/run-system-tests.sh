#!/bin/bash
set -euo pipefail

BUILD_DIR=${1-${PWD}}

export NANO_NODE_EXE=${BUILD_DIR}/nano_node
cd ../systest && ./RUNALL