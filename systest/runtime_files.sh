#!/bin/bash
set -eux

# Test runtime files: PID file and runtime info file
# Both files should be created on startup and cleaned up on exit

DATADIR=$(mktemp -d)
PID_FILE="$DATADIR/node.pid"
RUNTIME_INFO="$DATADIR/runtime_info.json"

cleanup() {
    kill $NODE_PID 2>/dev/null || true
    rm -rf "$DATADIR"
}
trap cleanup EXIT

# Start the node with both --pid_file and --runtime_info_file using ephemeral ports
$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --enable_rpc \
    --pid_file "$PID_FILE" \
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

# Verify PID file exists and contains correct PID
[ -f "$PID_FILE" ] || { echo "FAIL: PID file not created"; exit 1; }

FILE_PID=$(cat "$PID_FILE" | tr -d '\n')
[ "$FILE_PID" = "$NODE_PID" ] || { echo "FAIL: PID mismatch (expected $NODE_PID, got $FILE_PID)"; exit 1; }

# Verify runtime_info.json exists and contains required fields
[ -f "$RUNTIME_INFO" ] || { echo "FAIL: runtime_info.json not created"; exit 1; }

PEERING_PORT=$(jq -r '.peering_port' "$RUNTIME_INFO")
RPC_PORT=$(jq -r '.rpc_port' "$RUNTIME_INFO")
NODE_ID=$(jq -r '.node_id' "$RUNTIME_INFO")

# Verify ephemeral ports were assigned
[ "$PEERING_PORT" != "0" ] && [ -n "$PEERING_PORT" ] || { echo "FAIL: invalid peering_port"; exit 1; }
[ "$RPC_PORT" != "0" ] && [ -n "$RPC_PORT" ] && [ "$RPC_PORT" != "null" ] || { echo "FAIL: invalid rpc_port"; exit 1; }
[ -n "$NODE_ID" ] && [ "$NODE_ID" != "null" ] || { echo "FAIL: invalid node_id"; exit 1; }

# Verify peering port is listening
nc -z ::1 "$PEERING_PORT" || { echo "FAIL: Nothing listening on peering port $PEERING_PORT"; exit 1; }

# Verify RPC is responding using discovered port
curl -s -g -d '{ "action": "version" }' "[::1]:$RPC_PORT" | grep -q "node_vendor" || { echo "FAIL: RPC not responding"; exit 1; }

# Stop the node via SIGINT
kill -SIGINT $NODE_PID

# Wait for clean exit
wait $NODE_PID || { echo "FAIL: Node did not stop cleanly"; exit 1; }

# Verify cleanup
[ ! -f "$PID_FILE" ] || { echo "FAIL: PID file not cleaned up"; exit 1; }
[ ! -f "$RUNTIME_INFO" ] || { echo "FAIL: runtime_info.json not cleaned up"; exit 1; }

echo "All runtime_files tests passed"
