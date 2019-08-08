#!/bin/bash


set -Eeuo pipefail

usage() {
	echo -e \
	"Usage:\n" \
	"  $0 nano_node [daemon] [cli_options] [-l] [-v size]\n" \
	"    daemon\n" \
	"      start as daemon\n\n" \
	"    cli_options\n" \
	"      nano_node cli options <see nano_node --help>\n\n" \
	"    -l\n" \
	"      log to console <use docker logs {container}>\n\n" \
	"    -v<size>\n" \
	"      vacuum database if over size GB on startup\n\n" \
	"  $0 bash [other]\n" \
	"    other\n" \
	"      bash pass through\n" \
	"  $0 [*]\n" \
	"    *\n" \
	"      usage\n\n" \
	"default:\n" \
	"  $0 nano_node daemon -l"
}

OPTIND=1
command=()
IFS=' ' read -r -a TEMP_OPTS <<<"$@"
passthrough=()
db_size=0
log_to_cerr=0

if [ ${#TEMP_OPTS[@]} -lt 2 ]; then
	usage
	exit 1
fi

if [[ "${TEMP_OPTS[0]}" = 'nano_node' ]]; then
	unset 'TEMP_OPTS[0]'
	command+=("nano_node")
	shift;
	for i in "${TEMP_OPTS[@]}"; do
		case $i in
			"daemon" )
				command+=("--daemon")
				;;
			* )
				passthrough+=("$i")
				;;
		esac
	done
	for i in "${passthrough[@]}"; do
		if [[ "$i" =~ "-v" ]]; then
		        db_size=${i//-v/}
			echo "Vacuum DB if over $db_size GB on startup"
		elif [[ "$i" = '-l' ]]; then
			echo "\"log_to_cerr\":\"true\""
			log_to_cerr=1
		else
		 	command+=("$i")
		fi
	done
elif [[ "${TEMP_OPTS[0]}" = 'bash' ]]; then
	unset 'TEMP_OPTS[0]'
	echo -e "EXECUTING ${TEMP_OPTS[*]}\n"
	exec "${TEMP_OPTS[@]}"
	exit 0;
else
	usage
	exit 1;
fi

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
nanodir="${HOME}/Nano${dirSuffix}"
dbFile="${nanodir}/data.ldb"

if [ -d "${raidir}" ]; then
	echo "Moving ${raidir} to ${nanodir}"
	mv "$raidir" "$nanodir"
else
	mkdir -p "${nanodir}"
fi

if [ ! -f "${nanodir}/config.json" ]; then
	echo "Config File not found, adding default."
	cp "/usr/share/nano/config/${network}.json" "${nanodir}/config.json"
	cp "/usr/share/nano/config/${network}_rpc.json" "${nanodir}/rpc_config.json"
fi

if [[ $log_to_cerr -eq 1 ]]; then
	sed -i 's/"log_to_cerr": "false",/"log_to_cerr": "true",/g' "${nanodir}/config.json"
else
	sed -i 's/"log_to_cerr": "true",/"log_to_cerr": "false",/g' "${nanodir}/config.json"
fi

if [[ "${command[1]}" = "--daemon" ]]; then
	if [[ $db_size -ne 0 ]]; then
		if [ -f "${dbFile}" ]; then
			dbFileSize="$(stat -c %s "${dbFile}" 2>/dev/null)"
			if [ "${dbFileSize}" -gt $((1024 * 1024 * 1024 * db_size)) ]; then
				echo "ERROR: Database size grew above ${db_size}GB (size = ${dbFileSize})" >&2
				nano_node --vacuum
			fi
		fi
	fi
fi
echo -e "EXECUTING: ${command[*]}\n"
exec "${command[@]}"
