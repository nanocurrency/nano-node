#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

network="$(cat /etc/nano-network)"
case "${network}" in
        live|'')
                network='live'
                dirSuffix=''
                ;;
        beta)
                dirSuffix='Beta'
                ;;
        test)
                dirSuffix='Teta'
                ;;
esac

nanodir="${HOME}/RaiBlocks${dirSuffix}"
mkdir -p "${nanodir}"
if [ ! -f "${nanodir}/config.json" ]; then
        echo "Config File not found, adding default."
        cp "/usr/share/raiblocks/config/${network}.json" "${nanodir}/config.json"
fi

/usr/bin/rai_node --daemon
