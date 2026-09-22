#!/bin/bash
set -euo pipefail

script_dir=$(dirname "${BASH_SOURCE[0]}")
source "${script_dir}/common.sh"

# The suite runs as parallel gtest shards. Tests take OS-assigned ports and random data
# directories, so shards don't collide. Override the shard count with CORE_TEST_JOBS.
jobs=${CORE_TEST_JOBS:-$(get_processor_count)}

# Runs core_test split across `jobs` shards and fails if any shard fails.
# Each output line is prefixed with its shard, since the shards stream concurrently.
# Workflow commands (`::error::` etc.) are left unprefixed so GitHub still parses them.
run_sharded() {
    local pids=()
    local i
    for ((i = 0; i < jobs; i++)); do
        (
            export GTEST_TOTAL_SHARDS="${jobs}" GTEST_SHARD_INDEX="${i}"
            # Explicit ports would otherwise be handed out from the same range in every shard.
            if [ -n "${NANO_TEST_BASE_PORT-}" ]; then
                export NANO_TEST_BASE_PORT=$((NANO_TEST_BASE_PORT + i * 200))
            fi
            "${script_dir}/run-tests.sh" core_test "$@" 2>&1 | awk -v prefix="[shard ${i}] " '{ print (/^::/ ? "" : prefix) $0; fflush() }'
        ) &
        pids+=($!)
    done

    local status=0
    for i in "${!pids[@]}"; do
        if ! wait "${pids[$i]}"; then
            echo "::error::core_test shard ${i} of ${jobs} failed"
            status=1
        fi
    done
    return "${status}"
}

echo "Running core_test in ${jobs} shards"

# Without dump collection there is nothing to opt out of, so run the suite in one go.
if [ -z "${COREDUMP_DIR-}" ]; then
    run_sharded
    exit
fi

# Suites suffixed `DeathTest` crash child processes on purpose, and every crash writes a
# multi-gigabyte dump - enough to fill the runner's disk before the rest of the suite
# starts. Run them in a separate pass with core dumps turned off.
# The pattern covers plain suites (`foo_DeathTest.bar`) and the typed/parameterized
# instantiations gtest names `foo_DeathTest/0.bar`.
death_test_filter='*DeathTest.*:*DeathTest/*.*'

# Guard the naming convention: if it ever drifts, the death tests would silently fall
# into the dump-collecting pass and fill the disk again with nothing reporting it.
listed=$("$(get_test_executable core_test)" --gtest_list_tests --gtest_filter="${death_test_filter}")
case "${listed}" in
    *DeathTest*) ;;
    *)
        echo "::error::No test suites matched '${death_test_filter}'"
        exit 1
        ;;
esac

status=0

NANO_DISABLE_CORE_DUMPS=1 run_sharded --gtest_filter="${death_test_filter}" || status=$?

run_sharded --gtest_filter="-${death_test_filter}" || status=$?

exit "${status}"
