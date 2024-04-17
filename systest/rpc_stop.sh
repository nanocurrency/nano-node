#!/bin/bash
set -eux

DATADIR=$(mktemp -d)

# Start the node in daemon mode in the background
$NANO_NODE_EXE --daemon --network dev --data_path $DATADIR --config rpc.enable=true --rpcconfig enable_control=true &
NODE_PID=$!

# Allow some time for the node to start up completely
sleep 10

# Send the stop rpc command
curl -g -d '{ "action": "stop" }' '[::1]:45000'

# Check if the process has stopped using a timeout to avoid infinite waiting
if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "Node did not stop as expected"
    exit 1
fi
