#!/bin/bash
set -euo pipefail

script_dir=$(dirname "${BASH_SOURCE[0]}")
source "${script_dir}/common.sh"

# Without dump collection there is nothing to opt out of, so run the suite in one go.
if [ -z "${COREDUMP_DIR-}" ]; then
    exec "${script_dir}/run-tests.sh" core_test
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

NANO_DISABLE_CORE_DUMPS=1 "${script_dir}/run-tests.sh" core_test --gtest_filter="${death_test_filter}" || status=$?

"${script_dir}/run-tests.sh" core_test --gtest_filter="-${death_test_filter}" || status=$?

exit "${status}"
