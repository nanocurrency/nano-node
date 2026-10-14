#!/bin/bash
set -eux

# Test that the daemon handles SIGINT gracefully

source "$(dirname "$0")/lib/common.sh"

DATADIR=$(new_datadir)
RUNTIME_INFO="$DATADIR/runtime_info.json"

# Start the node in daemon mode with ephemeral port
$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 &
NODE_PID=$!
register_pid $NODE_PID

# Wait for runtime_info.json to appear (indicates node is ready)
wait_for_file "$RUNTIME_INFO" $NODE_PID

# Send an interrupt signal to the node process
kill -SIGINT $NODE_PID

# Check if the process has stopped using a timeout to avoid infinite waiting
if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "Node did not stop as expected"
    exit 1
fi
