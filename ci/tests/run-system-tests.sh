#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

# Path to the nano-node repository can be provided as an argument
# Otherwise parent directory of working directory is assumed
NANO_REPO_DIR=${1:-../}
NANO_SYSTEST_DIR=${NANO_REPO_DIR}/systest

echo "Running systests from: ${NANO_SYSTEST_DIR}"

# This assumes that the executables are in the current working directory
export NANO_NODE_EXE=./nano_node$(get_exec_extension)
export NANO_RPC_EXE=./nano_rpc$(get_exec_extension)

overall_status=0

for script in ${NANO_SYSTEST_DIR}/*.sh; do
    name=$(basename ${script})

    echo "::group::Running: $name"

    # Redirecting output to a file to prevent it from being mixed with the output of the action
    ./$script > "${name}.log" 2>&1
    status=$?
    cat "${name}.log"

    echo "::endgroup::"

    if [ $status -eq 0 ]; then
        echo "Passed: $name"
    else
        echo "::error Systest failed: $name ($?)"
        overall_status=1
    fi
done

if [ $overall_status -eq 0 ]; then
    echo "::notice::All systests passed"
else
    echo "::error::Some systests failed"
    exit 1
fi
