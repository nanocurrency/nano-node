#!/bin/bash
set -eux

# A daemon with node.extended_ledger_index enabled populates the indices at startup,
# serves the indexed delegators API over RPC, and persists the index flags across shutdown

source "$(dirname "$0")/lib/common.sh"

DATADIR=$(new_datadir)
RUNTIME_INFO="$DATADIR/runtime_info.json"

$NANO_NODE_EXE --daemon --network dev --data_path "$DATADIR" \
    --enable_rpc \
    --runtime_info_file "$RUNTIME_INFO" \
    --config node.peering_port=0 \
    --config node.extended_ledger_index=true \
    --rpcconfig port=0 &
NODE_PID=$!
register_pid $NODE_PID

wait_for_file "$RUNTIME_INFO" $NODE_PID 30

RPC_PORT=$(jq -r '.rpc_port' "$RUNTIME_INFO")
[ -n "$RPC_PORT" ] && [ "$RPC_PORT" != "0" ] && [ "$RPC_PORT" != "null" ]

# The only representative on a fresh ledger is genesis; discover its account
GENESIS=$(curl -s -g -d '{ "action": "representatives" }' "[::1]:$RPC_PORT" | jq -r '.representatives | keys[0]')
[ -n "$GENESIS" ] && [ "$GENESIS" != "null" ]

# The indexed delegators API serves genesis delegating its full weight to itself
curl -s -g -d "{ \"action\": \"delegators\", \"account\": \"$GENESIS\" }" "[::1]:$RPC_PORT" \
    | jq -e --arg acc "$GENESIS" '.delegators[$acc] == "340282366920938463463374607431768211455"'

# Stop the node and verify the persisted flags survive the daemon
kill -SIGINT $NODE_PID
wait $NODE_PID

$NANO_NODE_EXE --database_info --data_path "$DATADIR" --network dev | grep -Eq 'account_delegator_by_weight_index: +enabled'

echo "All ledger_extended_index_daemon tests passed"
