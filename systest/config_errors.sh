#!/bin/bash
# Broken configuration must be reported with the file, the key and the problem, never as an uncaught exception.
# Covers the single handler in main, the daemon's own handler, a command that reports and continues, and warnings for unknown keys.
source "$(dirname "$0")/lib/common.sh"
trap systest_cleanup EXIT

# Runs a command expected to fail cleanly and checks its combined output for every expected text
expect_failure() {
    local name="$1"; shift
    local -a expected=()
    while [ "$1" != "--" ]; do expected+=("$1"); shift; done
    shift
    set +e
    OUTPUT=$("$@" 2>&1)
    local code=$?
    set -e
    if echo "$OUTPUT" | grep -q "terminate called\|libc++abi"; then
        echo "FAIL: $name crashed with an uncaught exception"; echo "$OUTPUT"; exit 1
    fi
    if [ "$code" -eq 0 ] || [ "$code" -ge 128 ]; then
        echo "FAIL: $name exited with code $code"; echo "$OUTPUT"; exit 1
    fi
    check_output "$name" "${expected[@]}"
}

# Runs a command expected to succeed and checks its combined output for every expected text
expect_success() {
    local name="$1"; shift
    local -a expected=()
    while [ "$1" != "--" ]; do expected+=("$1"); shift; done
    shift
    set +e
    OUTPUT=$("$@" 2>&1)
    local code=$?
    set -e
    if [ "$code" -ne 0 ]; then
        echo "FAIL: $name exited with code $code"; echo "$OUTPUT"; exit 1
    fi
    check_output "$name" "${expected[@]}"
}

check_output() {
    local name="$1"; shift
    for text in "$@"; do
        if ! echo "$OUTPUT" | grep -qF -- "$text"; then
            echo "FAIL: $name output lacks '$text'"; echo "$OUTPUT"; exit 1
        fi
    done
    echo "PASS: $name"
}

DATADIR=$(new_datadir)
NODE="$NANO_NODE_EXE --network dev --data_path $DATADIR"

# A syntax error is reported through main with the file and line
printf '[node\n' > "$DATADIR/config-node.toml"
expect_failure "syntax error in CLI command" "Error: config-node.toml:" "line 1" -- $NODE --wallet_list
expect_failure "syntax error in --update_config" "Error: config-node.toml:" "line 1" -- $NODE --update_config
rm "$DATADIR/config-node.toml"

# A malformed override is quoted back with the parser's complaint
expect_failure "malformed override" 'Invalid config override "node..foo' "Bare key missing name" -- $NODE --wallet_list --config node..foo=1

# A value out of range or of the wrong type names the key; every problem is listed at once
printf '[node]\nio_threads = 0\nenable_voting = "yes"\n' > "$DATADIR/config-node.toml"
expect_failure "invalid values in CLI command" "config-node.toml:" "enable_voting is not a boolean" -- $NODE --wallet_list
rm "$DATADIR/config-node.toml"
expect_failure "invalid values in daemon" "config-node.toml:" "io_threads must be non-zero" -- $NODE --daemon --config node.io_threads=0 --config node.peering_port=0

# Flags that contradict the config are rejected before the node starts
expect_failure "conflicting flags in daemon" "--enable_pruning" -- $NODE --daemon --enable_pruning --config node.enable_voting=true --config node.peering_port=0

# A command with its own error reporting still names the file and the problem
printf '[node]\nio_threads = 0\n' > "$DATADIR/config-node.toml"
set +e
OUTPUT=$($NODE --database_info 2>&1)
set -e
check_output "invalid values in --database_info" "config-node.toml: io_threads must be non-zero"
rm "$DATADIR/config-node.toml"

# Unknown keys are warned about by dotted path and the command proceeds
expect_success "unknown key warning" 'Warning: unknown key `node.typo` in config-node.toml' -- $NODE --initialize --config node.typo=1
