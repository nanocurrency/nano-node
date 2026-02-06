#!/bin/bash
set -euo pipefail

TEST_TYPE=${1:-}
if [ -z "${TEST_TYPE}" ]; then
    echo "Usage: $0 <test-type>"
    echo "  test-type: core, rpc, system"
    exit 1
fi

echo "Running ${TEST_TYPE} tests in Docker"

# Run tests inside the pre-built Docker image
# --privileged: Required for core dump generation (writing to /proc/sys/kernel/core_pattern)
# --tmpfs: Provides fast RAM-based storage for test data (required by tests via NANO_APP_PATH)
# --network=host: Use host networking for IPC tests that connect to localhost
docker run --rm \
    --privileged \
    --network=host \
    --tmpfs /tmp:exec,size=4G \
    -e NANO_APP_PATH="/tmp" \
    -e NANO_BACKEND \
    -e TEST_USE_ROCKSDB \
    -e DEADLINE_SCALE_FACTOR \
    nano-test:latest \
    bash -c "source ci/tests/setup-core-dumps.sh && cd build && ../ci/tests/run-${TEST_TYPE}-tests.sh"
