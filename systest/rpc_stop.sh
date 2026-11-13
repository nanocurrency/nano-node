#!/bin/bash
set -eux

# Test that the RPC stop command gracefully shuts down the node

source "$(dirname "$0")/lib/common.sh"

DATADIR=$(new_datadir)
RUNTIME_INFO="$DATADIR/runtime_info.json"

# Start the node in daemon mode with ephemeral ports
$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --enable_rpc \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 \
    --rpcconfig port=0 \
    --rpcconfig enable_control=true &
NODE_PID=$!
register_pid $NODE_PID

# Wait for runtime_info.json to appear (indicates node is ready)
wait_for_file "$RUNTIME_INFO" $NODE_PID

# Get the RPC port from runtime info
RPC_PORT=$(jq -r '.rpc_port' "$RUNTIME_INFO")
[ "$RPC_PORT" != "0" ] && [ -n "$RPC_PORT" ] && [ "$RPC_PORT" != "null" ] || { echo "FAIL: invalid rpc_port"; exit 1; }

# Send the stop rpc command, the node must acknowledge it before shutting down
RESPONSE=$(curl -g -s -d '{ "action": "stop" }' "[::1]:$RPC_PORT")
echo "$RESPONSE"
if ! echo "$RESPONSE" | grep -q '"success"'; then
    echo "FAIL: stop request was not acknowledged"
    exit 1
fi

# Check if the process has stopped using a timeout to avoid infinite waiting
if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "Node did not stop as expected"
    exit 1
fi
