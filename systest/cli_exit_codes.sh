#!/bin/bash
set -eux

source "$(dirname "$0")/lib/common.sh"

DATADIR=$(new_datadir)

# commands that succeed exit with 0
$NANO_NODE_EXE --key_create --data_path $DATADIR > /dev/null
$NANO_NODE_EXE --generate_config node --data_path $DATADIR > /dev/null

# commands that print an error exit with 1, any other status such as a crash fails the test
expect_failure() {
    local status=0
    $NANO_NODE_EXE "$@" --data_path $DATADIR || status=$?
    if [ "$status" -ne 1 ]; then
        echo "FAIL: nano_node $* exited with $status, expected 1" >&2
        exit 1
    fi
}
expect_failure --key_expand
expect_failure --key_expand --key invalid
expect_failure --generate_config invalid

# a node config that cannot be parsed fails the commands that read it
echo "[node" > $DATADIR/config-node.toml
expect_failure --database_info

# if it got this far then it is a pass
exit 0
