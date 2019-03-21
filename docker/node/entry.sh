#!/bin/bash

PATH="${PATH:-/bin}:/usr/bin"
export PATH

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
                dirSuffix='Test'
                ;;
esac

raidir="${HOME}/RaiBlocks${dirSuffix}"
nanodir="${HOME}/BananoData${dirSuffix}"
dbFile="${nanodir}/data.ldb"

if [ -d "${raidir}" ]; then
	echo "Moving ${raidir} to ${nanodir}"
	mv $raidir $nanodir
else
	mkdir -p "${nanodir}"
fi

if [ ! -f "${nanodir}/config.json" ]; then
        echo "Config File not found, adding default."
        cp "/usr/share/nano/config/${network}.json" "${nanodir}/config.json"
fi

# Start watching the log file we are going to log output to
logfile="${nanodir}/nano-docker-output.log"
tail -F "${logfile}" &

pid=''
firstTimeComplete=''
while true; do
	if [ -n "${firstTimeComplete}" ]; then
		sleep 10
	fi
	firstTimeComplete='true'

	if [ -f "${dbFile}" ]; then
		dbFileSize="$(stat -c %s "${dbFile}" 2>/dev/null)"
		if [ "${dbFileSize}" -gt $[1024 * 1024 * 1024 * 20] ]; then
			echo "ERROR: Database size grew above 20GB (size = ${dbFileSize})" >&2

			while [ -n "${pid}" ]; do
				kill "${pid}" >/dev/null 2>/dev/null || :
				if ! kill -0 "${pid}" >/dev/null 2>/dev/null; then
					pid=''
				fi
			done

			banannode --vacuum
		fi
	fi

	if [ -n "${pid}" ]; then
		if ! kill -0 "${pid}" >/dev/null 2>/dev/null; then
			pid=''
		fi
	fi

	if [ -z "${pid}" ]; then
		bananode --daemon &
		pid="$!"
	fi

	if [ "$(stat -c '%s' "${logfile}")" -gt 4194304 ]; then
		cp "${logfile}" "${logfile}.old"
		: > "${logfile}"
		echo "$(date) Rotated log file"
	fi
done >> "${logfile}" 2>&1
