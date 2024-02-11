#!/bin/sh

set -e

DATADIR=data.systest

# the caller should set the env var NANO_NODE_EXE to point to the nano_node executable
# if NANO_NODE_EXE is unser ot empty then "../../build/nano_node" is used
NANO_NODE_EXE=${NANO_NODE_EXE:-../../build/nano_node}

clean_data_dir() {
    rm -f  "$DATADIR"/log/log_*.log
    rm -f  "$DATADIR"/wallets.ldb*
    rm -f  "$DATADIR"/data.ldb*
    rm -f  "$DATADIR"/config-*.toml
    rm -rf "$DATADIR"/rocksdb/
}

test_initialize_cmd() {
    netmatch="$1"
    netcmd="$2"
    netarg="$3"
    genesishash="$4"

    clean_data_dir

    # initialise data directory
    $NANO_NODE_EXE --initialize --data_path "$DATADIR" "$netcmd" "$netarg"

    # check that it is the live network
    grep -q "Active network: $netmatch" "$DATADIR"/log/log_*.log

    # check that the ledger file is created and has one block, the genesis block
    $NANO_NODE_EXE --debug_block_count --data_path "$DATADIR" "$netcmd" "$netarg" | grep -q 'Block count: 1'

    # check the genesis block is correct
    $NANO_NODE_EXE --debug_block_dump --data_path "$DATADIR" "$netcmd" "$netarg" | head -n 1 | grep -qi "$genesishash"
}

mkdir -p "$DATADIR/log"

#test_initialize_cmd "live" ""          ""     "991CF190094C00F0B68E2E5F75F6BEE95A2E0BD93CEAA4A6734DB9F19B728948"
test_initialize_cmd "live" "--network" "live" "991CF190094C00F0B68E2E5F75F6BEE95A2E0BD93CEAA4A6734DB9F19B728948"
test_initialize_cmd "beta" "--network" "beta" "E1227CF974C1455A8B630433D94F3DDBF495EEAC9ADD2481A4A1D90A0D00F488"
test_initialize_cmd "test" "--network" "test" "B1D60C0B886B57401EF5A1DAA04340E53726AA6F4D706C085706F31BBD100CEE"

# if it got this far then it is a pass
echo $0: PASSED
exit 0
