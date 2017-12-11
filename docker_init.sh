#!/bin/bash
set -e

DATA_DIRECTORY="/root/RaiBlocks"

# check if data directory exist
if [[ ! $(ls -A "$DATA_DIRECTORY" 2> /dev/null) ]]; then
  # this command is going to fail, but needed to initiate the data directory
  /usr/local/bin/rai_node --daemon 2> /dev/null ||

  echo "please ignore any errors above this line..."

  # replace RPC address in config.json for docker
  sed -i "s/\"address\": \"::1\"/\"address\": \"::ffff:0.0.0.0\"/g" "$DATA_DIRECTORY"/config.json
fi

if [[ $1 ]]; then
  echo "executing command $1..."
  exec $1
else
  echo "running rai_node --daemon..."
  exec /usr/local/bin/rai_node --daemon
fi
