#!/bin/bash
set -eux

# Test that a stop request sent to the nano_rpc child process is acknowledged, stops the node,
# and lets the child exit on its own rather than being terminated by the node

source "$(dirname "$0")/lib/common.sh"

if [ -z "${NANO_RPC_EXE-}" ] || [ ! -x "$NANO_RPC_EXE" ]; then
    echo "SKIP: NANO_RPC_EXE is not set or not executable"
    exit 0
fi
NANO_RPC_EXE="$(cd "$(dirname "$NANO_RPC_EXE")" && pwd)/$(basename "$NANO_RPC_EXE")"

DATADIR=$(new_datadir)
RUNTIME_INFO="$DATADIR/runtime_info.json"

# The child reads its ports from the RPC config file, so they cannot be ephemeral
RPC_PORT=$((20000 + RANDOM % 20000))
IPC_PORT=$((40000 + RANDOM % 20000))
cat > "$DATADIR/config-rpc.toml" <<EOF
enable_control = true
port = $RPC_PORT
[process]
ipc_port = $IPC_PORT
EOF

$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --enable_rpc \
    --runtime_info_file "$RUNTIME_INFO" \
    --config rpc.child_process.enable=true \
    --config "rpc.child_process.rpc_path=$NANO_RPC_EXE" \
    --config node.ipc.tcp.enable=true \
    --config node.ipc.tcp.port=$IPC_PORT \
    --config node.peering_port=0 &
NODE_PID=$!
register_pid $NODE_PID

wait_for_file "$RUNTIME_INFO" $NODE_PID

# Wait for the child to serve requests
RESPONSE=""
for i in {1..60}; do
    RESPONSE=$(curl -g -s -m 2 -d '{ "action": "version" }' "[::1]:$RPC_PORT" || true)
    [ -n "$RESPONSE" ] && break
    sleep 0.5
done
if [ -z "$RESPONSE" ]; then
    echo "FAIL: RPC child process did not start serving requests"
    exit 1
fi

RESPONSE=$(curl -g -s -m 5 -d '{ "action": "stop" }' "[::1]:$RPC_PORT")
echo "$RESPONSE"
if ! echo "$RESPONSE" | grep -q '"success"'; then
    echo "FAIL: stop request was not acknowledged"
    exit 1
fi

if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "FAIL: node did not stop cleanly"
    exit 1
fi

if grep -q "did not exit on its own" "$DATADIR"/log/log_*.log; then
    echo "FAIL: the RPC child process had to be terminated"
    exit 1
fi
echo "RPC child process exited on its own"
