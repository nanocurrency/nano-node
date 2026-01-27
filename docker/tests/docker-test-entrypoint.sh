#!/bin/bash
set -uo pipefail

# Default entrypoint for running all tests locally
# For CI, individual tests are run via ci/tests/run-docker-tests.sh <test-type>

exit_code=0

cd build

echo "=== Running Core Tests ==="
if ../ci/tests/run-core-tests.sh; then
    echo "Core tests: PASSED"
else
    echo "Core tests: FAILED"
    exit_code=1
fi

echo "=== Running RPC Tests ==="
if ../ci/tests/run-rpc-tests.sh; then
    echo "RPC tests: PASSED"
else
    echo "RPC tests: FAILED"
    exit_code=1
fi

echo "=== Running System Tests ==="
if ../ci/tests/run-system-tests.sh; then
    echo "System tests: PASSED"
else
    echo "System tests: FAILED"
    exit_code=1
fi

echo "=== Test Summary ==="
if [ $exit_code -eq 0 ]; then
    echo "All tests passed"
else
    echo "Some tests failed"
fi

exit $exit_code
