#!/bin/bash
set -euo pipefail
IFS=$'\n\t'

mkdir -p ~/RaiBlocks
if [ ! -f ~/RaiBlocks/config.json ]; then
  echo "Config File not found, adding default."
  cp /usr/share/raiblocks/config.json ~/RaiBlocks/
fi
/usr/bin/rai_node --daemon
