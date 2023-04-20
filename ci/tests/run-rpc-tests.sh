#!/bin/bash
set -euo pipefail

BUILD_DIR=${1-${PWD}}

${BUILD_DIR}/rpc_test