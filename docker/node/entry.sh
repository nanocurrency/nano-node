#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

network="$(cat /etc/banano-network)"
case "${network}" in
        live|'')
                network='live'
                dirSuffix=''
                ;;
        beta)
                dirSuffix='Beta'
                ;;
        test)
                dirSuffix='Test'
                ;;
esac

bananodir="${HOME}/Banano${dirSuffix}"
mkdir -p "${bananodir}"
if [ ! -f "${bananodir}/config.json" ]; then
        echo "Config File not found, adding default."
        cp "/usr/share/banano/config/${network}.json" "${bananodir}/config.json"
fi

/usr/bin/bananode --daemon
