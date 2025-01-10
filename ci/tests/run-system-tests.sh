#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

# Path to the celerix-node repository can be provided as an argument
# Otherwise parent directory of working directory is assumed
CELERIX_REPO_DIR=${1:-../}
CELERIX_SYSTEST_DIR=${CELERIX_REPO_DIR}/systest

# Allow TEST_TIMEOUT to be set from an environment variable
TEST_TIMEOUT=${TEST_TIMEOUT:-300s}

echo "Running systests from: ${CELERIX_SYSTEST_DIR}"

# This assumes that the executables are in the current working directory
export CELERIX_NODE_EXE=./celerix_node$(get_exec_extension)
export CELERIX_RPC_EXE=./celerix_rpc$(get_exec_extension)

overall_status=0

for script in ${CELERIX_SYSTEST_DIR}/*.sh; do
    name=$(basename ${script})

    echo "::group::Running: $name"

    # Redirecting output to a file to prevent it from being mixed with the output of the action
    # Using timeout command to enforce time limits
    timeout $TEST_TIMEOUT ./$script > "${name}.log" 2>&1
    status=$?
    cat "${name}.log"

    echo "::endgroup::"

    if [ $status -eq 0 ]; then
        echo "Passed: $name"
    elif [ $status -eq 124 ]; then
        echo "::error::Systest timed out: $name"
        overall_status=1
    else
        echo "::error::Systest failed: $name ($status)"
        overall_status=1
    fi
done

if [ $overall_status -eq 0 ]; then
    echo "All systests passed"
else
    echo "::error::Some systests failed"
    exit 1
fi
