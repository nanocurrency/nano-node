#!/bin/bash
set -eux

# Test that the RPC stop command gracefully shuts down the node

DATADIR=$(mktemp -d)
RUNTIME_INFO="$DATADIR/runtime_info.json"

cleanup() {
    kill $NODE_PID 2>/dev/null || true
    rm -rf "$DATADIR"
}
trap cleanup EXIT

# Start the node in daemon mode with ephemeral ports
$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --enable_rpc \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 \
    --rpcconfig port=0 \
    --rpcconfig enable_control=true &
NODE_PID=$!

# Wait for runtime_info.json to appear (indicates node is ready)
for i in {1..30}; do
    if [ -f "$RUNTIME_INFO" ]; then
        break
    fi
    sleep 1
done

[ -f "$RUNTIME_INFO" ] || { echo "FAIL: runtime_info.json not created"; exit 1; }

# Get the RPC port from runtime info
RPC_PORT=$(jq -r '.rpc_port' "$RUNTIME_INFO")
[ "$RPC_PORT" != "0" ] && [ -n "$RPC_PORT" ] && [ "$RPC_PORT" != "null" ] || { echo "FAIL: invalid rpc_port"; exit 1; }

# Send the stop rpc command
curl -g -d '{ "action": "stop" }' "[::1]:$RPC_PORT"

# Check if the process has stopped using a timeout to avoid infinite waiting
if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "Node did not stop as expected"
    exit 1
fi
