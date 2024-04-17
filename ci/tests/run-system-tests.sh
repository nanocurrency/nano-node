#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

# Path to the nano-node repository can be provided as an argument
# Otherwise parent directory of working directory is assumed
NANO_REPO_DIR=${1:-../}
NANO_SYSTEST_DIR=${NANO_REPO_DIR}/systest

# Allow TEST_TIMEOUT to be set from an environment variable
TEST_TIMEOUT=${TEST_TIMEOUT:-300s}

echo "Running systests from: ${NANO_SYSTEST_DIR}"

# This assumes that the executables are in the current working directory
export NANO_NODE_EXE=./nano_node$(get_exec_extension)
export NANO_RPC_EXE=./nano_rpc$(get_exec_extension)

overall_status=0

for script in ${NANO_SYSTEST_DIR}/*.sh; do
    name=$(basename ${script})

    echo "::group::Running: $name"

    # Redirecting output to a file to prevent it from being mixed with the output of the action
    # Using timeout command to enforce time limits
    if [[ "$OSTYPE" == "msys" ]]; then
        # Windows minimal system (msys) detected. Launch a command prompt for better compatibility
		# Todo: Add timeout logic to limit execution time. This will probably require a powershell script
        cmd.exe /C "./$script > ${name}.log 2>&1"
    else
        # Other systems like Mac or Linux
        timeout $TEST_TIMEOUT ./$script > "${name}.log" 2>&1
    fi
    
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
    echo "::notice::All systests passed"
else
    echo "::error::Some systests failed"
    exit 1
fi
