#!/bin/bash
set -eux

DATADIR=$(mktemp -d)

SEED=CEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEEDCEED

# initialise data directory
$NANO_NODE_EXE --initialize --data_path $DATADIR

# create a wallet and store the wallet ID
wallet_id=`$NANO_NODE_EXE --wallet_create --data_path $DATADIR --seed $SEED`

# decrypt the wallet and check the seed
$NANO_NODE_EXE --wallet_decrypt_unsafe --wallet $wallet_id --data_path $DATADIR | grep -q "Seed: $SEED"

# list the wallet and check the wallet ID
$NANO_NODE_EXE --wallet_list --data_path $DATADIR | grep -q "Wallet ID: $wallet_id"

# if it got this far then it is a pass
exit 0
