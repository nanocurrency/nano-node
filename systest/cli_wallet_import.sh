#!/bin/bash
set -euo pipefail

source "$(dirname "$0")/lib/common.sh"
datadir=$(new_datadir)
cli() {
    "$NANO_NODE_EXE" --network dev --data_path "$datadir" "$@"
}
rpc() {
    cli --debug_rpc <<< "$1"
}

cli --initialize
seed=1111111111111111111111111111111111111111111111111111111111111111
source_wallet=$(cli --wallet_create --seed "$seed")
destination=$(cli --wallet_create --seed "$seed")
original=$(rpc "{\"action\":\"account_list\",\"wallet\":\"$destination\"}" | jq -er '.accounts[0]')
additional=$(rpc "{\"action\":\"account_create\",\"wallet\":\"$source_wallet\",\"work\":false}" | jq -er '.account')
rpc "{\"action\":\"wallet_export\",\"wallet\":\"$source_wallet\"}" | jq -er '.json' > "$datadir/backup.json"

# Both wallets own the first seed account. The backup also holds a second account.
cli --wallet_import --wallet "$destination" --file "$datadir/backup.json"
rpc "{\"action\":\"wallet_info\",\"wallet\":\"$destination\"}" | jq -e '.deterministic_count == "1" and .adhoc_count == "1"' > /dev/null

# Replacing the seed clears its derived account while retaining the additional imported key.
rpc "{\"action\":\"wallet_change_seed\",\"wallet\":\"$destination\",\"seed\":\"2222222222222222222222222222222222222222222222222222222222222222\"}" | jq -e '.success == ""' > /dev/null
rpc "{\"action\":\"wallet_contains\",\"wallet\":\"$destination\",\"account\":\"$original\"}" | jq -e '.exists == "0"' > /dev/null
rpc "{\"action\":\"wallet_contains\",\"wallet\":\"$destination\",\"account\":\"$additional\"}" | jq -e '.exists == "1"' > /dev/null
