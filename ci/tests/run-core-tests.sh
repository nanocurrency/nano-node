#!/bin/bash
set -euo pipefail

script_dir=$(dirname "$BASH_SOURCE")

if [ -z "${COREDUMP_DIR-}" ]; then
    exec "${script_dir}/run-tests.sh" core_test
fi

# Match ordinary and typed death-test suites using Google Test's naming convention.
death_test_filter='*DeathTest.*:*DeathTest/*.*'
status=0

# Expected child crashes must not fill the runner's disk with core dumps.
(
    unset COREDUMP_DIR
    ulimit -S -c 0 || exit
    "${script_dir}/run-tests.sh" core_test --gtest_filter="${death_test_filter}"
) || status=$?

"${script_dir}/run-tests.sh" core_test --gtest_filter="-${death_test_filter}" || status=$?

exit "${status}"
