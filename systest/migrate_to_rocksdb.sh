#!/bin/bash
set -eux

# Test database migration from LMDB to RocksDB

DATADIR=$(mktemp -d)

# Force lmdb backend
NANO_BACKEND=lmdb $NANO_NODE_EXE --initialize --data_path $DATADIR --network test

if [ ! -f "$DATADIR/data.ldb" ]; then
	echo "ERROR: LMDB data.ldb file not found"
	exit 1
fi

$NANO_NODE_EXE --data_path $DATADIR --network test --migrate_database_lmdb_to_rocksdb

if [ ! -d "$DATADIR/rocksdb" ]; then
	echo "ERROR: RocksDB directory not found"
	exit 1
fi

exit 0
