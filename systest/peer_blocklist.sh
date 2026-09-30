#!/bin/bash
# The daemon reads peer-blocklist.toml from the data directory: an invalid entry stops it from starting,
# a valid file is reported in the log, and --generate_config prints a template for it.

source "$(dirname "$0")/lib/common.sh"

DATADIR=$(new_datadir)
RUNTIME_INFO="$DATADIR/runtime_info.json"

# The template names both lists
$NANO_NODE_EXE --generate_config blocklist | grep -q "^\[blocklist\]" || { echo "FAIL: template lacks the blocklist table"; exit 1; }
$NANO_NODE_EXE --generate_config blocklist | grep -q "node_ids" || { echo "FAIL: template lacks node_ids"; exit 1; }
$NANO_NODE_EXE --generate_config blocklist | grep -q "ip_addresses" || { echo "FAIL: template lacks ip_addresses"; exit 1; }
$NANO_NODE_EXE --generate_config blocklist | grep -q "peer-blocklist.toml" || { echo "FAIL: template does not name its file"; exit 1; }

# An invalid entry stops the daemon with an error naming the file and the entry
cat > "$DATADIR/peer-blocklist.toml" <<EOF
[blocklist]
node_ids = ["node_invalid"]
EOF
set +e
OUTPUT=$($NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" --config node.peering_port=0 2>&1)
EXIT_CODE=$?
set -e
if [ "$EXIT_CODE" -eq 0 ]; then
    echo "FAIL: daemon started with an invalid peer blocklist"
    exit 1
fi
if [ "$EXIT_CODE" -ge 128 ]; then
    echo "FAIL: daemon terminated by a signal (exit code $EXIT_CODE)"
    echo "$OUTPUT"
    exit 1
fi
echo "$OUTPUT" | grep -q "peer-blocklist.toml: Invalid node id: node_invalid" || { echo "FAIL: error does not name the file and the entry"; echo "$OUTPUT"; exit 1; }

# A valid file is loaded and reported at startup
cat > "$DATADIR/peer-blocklist.toml" <<EOF
# Peers refused by this node
[blocklist]
node_ids = ["node_1111111111111111111111111111111111111111111111111111hifc8npp"]
ip_addresses = ["192.0.2.1", "2001:db8::1"]
EOF
$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 &
NODE_PID=$!
register_pid $NODE_PID
wait_for_file "$RUNTIME_INFO" $NODE_PID

kill -SIGINT $NODE_PID
wait $NODE_PID || { echo "FAIL: daemon did not stop cleanly"; exit 1; }

# The log is complete once the daemon has exited
grep -q "Peer blocklist: 1 node ids, 2 IP addresses" "$DATADIR"/log/log_*.log || { echo "FAIL: startup log does not report the loaded entries"; exit 1; }

echo "PASS: peer blocklist file is validated, loaded and reported"
