#!/bin/bash
set -euo pipefail

# Ensure that an argument is provided
if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <argument>"
  exit 1
fi

# Capture the argument
ARGUMENT="$1"

# Run the command with the argument
$(dirname "$BASH_SOURCE")/run-tests.sh slow_test --gtest_filter=flamegraph.${ARGUMENT}
