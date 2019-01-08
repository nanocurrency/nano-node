#! /usr/bin/env bash

# XXX:TODO: Write a tool that implements this
function emit_members () {
	local class

	class="$1"

	case "${class}" in
		'rai::node')
			cat << \_EOF_
rai::gap_cache gap_cache
rai::ledger ledger
rai::active_transactions active
rai::network network
rai::bootstrap_initiator bootstrap_initiator
rai::bootstrap_listener bootstrap
rai::peer_container peers
rai::node_observers observers
rai::signature_checker checker
rai::vote_processor vote_processor
rai::rep_crawler rep_crawler
rai::block_processor block_processor
rai::block_arrival block_arrival
rai::online_reps online_reps
rai::stat stats
rai::block_uniquer block_uniquer
rai::vote_uniquer vote_uniquer
rai::alarm alarm
_EOF_
			;;
		'rai::gap_cache')
			cat << \_EOF_
boost::multi_index_container blocks
_EOF_
			;;
		'rai::active_transactions')
			cat << \_EOF_
boost::multi_index_container roots
std::deque confirmed
std::unordered_map successors
_EOF_
			;;
		'rai::bootstrap_listener')
			cat << \_EOF_
std::unordered_map connections
_EOF_
			;;
		'rai::bootstrap_initiator')
			cat << \_EOF_
rai::bootstrap_attempt *attempt
_EOF_
			;;
		'rai::bootstrap_attempt')
			cat << \_EOF_
std::deque clients
std::deque pulls
std::vector bulk_push_targets
std::unordered_set lazy_blocks
std::unordered_map lazy_state_unknown
std::unordered_map lazy_balances
std::unordered_set lazy_keys
std::deque lazy_pulls
_EOF_
			;;
		'rai::alarm')
			cat << \_EOF_
std::priority_queue operations
_EOF_
			;;
		'rai::block_processor')
			cat << \_EOF_
std::deque state_blocks
std::deque blocks
std::unordered_set blocks_hashes
std::deque forced
_EOF_
			;;
		'rai::vote_processor')
			cat << \_EOF_
std::deque votes
_EOF_
			;;
		'rai::rep_crawler')
			cat << \_EOF_
std::unordered_set active
_EOF_
			;;
		'rai::signature_checker')
			cat << \_EOF_
std::deque checks
_EOF_
			;;
		'rai::block_arrival')
			cat << \_EOF_
boost::multi_index_container arrival
_EOF_
			;;
		'rai::vote_uniquer')
			cat << \_EOF_
std::unordered_map votes
_EOF_
			;;
	esac
}

function emit_memory_information () {
	local responseObject startAtType startAtName
	local type name printName
	local output

	responseObject="$1"
	startAtType="$2"
	startAtName="$3"

	emit_members "${startAtType}" | while read -r type name; do
		case "${name}" in
			'*'*)
				name="${startAtName}${name:1}->"
				;;
			*)
				name="${startAtName}${name}."
				;;
		esac

		output="$(emit_memory_information "${responseObject}" "${type}" "${name}")"

		if [ -z "${output}" ]; then
			printName="$(echo "${name}" | sed 's@ *()@_function@;s@->@.@g;s@\.$@@')"

			mutex=''
			totalSizeOutput=''
			case "${type}" in
				'boost::multi_index_container'|'std::priority_queue'|'std::deque'|'std::unordered_map'|'std::vector'|'std::unordered_set')
					# XXX:TODO: Find mutex name dynamically
					case "${printName}" in
						*_function)
							;;
						*.lazy_*)
							mutex="lazy_mutex"
							mutex="${startAtName}${mutex}"
							;;
						*)
							mutex="mutex"
							mutex="${startAtName}${mutex}"
							;;
					esac

					output="${name}size ()";
					;;
			esac

			case "${type}" in
				'std::priority_queue')
					#totalSizeOutput="sizeof(*$(echo "${name}" | sed -r 's@(->|\.)$@@') * ${output})"
					;;
				'boost::multi_index_container'|'std::deque'|'std::unordered_map'|'std::unordered_set'|'std::vector')
					totalSizeOutput="sizeof(*${name}begin ()) * ${output}"
					;;
			esac

			if [ -n "${output}" ]; then
				if [ -z "${totalSizeOutput}" ]; then
					totalSizeOutput='"<unknown>"'
				fi
				if [ -n "${mutex}" ]; then
					echo "MUTEX=${mutex}"
				fi

				guard_nullable "${output}" "${responseObject}.put (\"${printName}.count\", ${output});"$'\n'"${responseObject}.put (\"${printName}.size\", ${totalSizeOutput});"
			fi
		else
			echo "${output}"
		fi
	done
}

function guard_nullable () {
	local start code addTabs
	local nullCheck nullable line

	start="$1"
	code="$2"
	addTabs="$3"

	nullCheck="${start}"
	nullable="$(
		while [[ "${nullCheck}" =~ '->' ]]; do
			nullCheck="$(echo "${nullCheck}" | sed 's@->[^-]*$@@')"
			echo "${nullCheck}"
		done | tac
	)"

	while IFS='' read -r nullCheck; do
		if [ -z "${nullCheck}" ]; then
			continue
		fi

		echo "${addTabs}if (${nullCheck})"
		echo "${addTabs}{"
		addTabs+=$'\t'
	done <<<"${nullable}"

	while IFS='' read -r line; do
		echo "${addTabs}${line}"
	done <<<"${code}"

	while IFS='' read -r nullCheck; do
		if [ -z "${nullCheck}" ]; then
			continue
		fi

		addTabs="${addTabs:1}"
		echo "${addTabs}}"
	done <<<"${nullable}"
}

# XXX:TODO: Hardcoded the response object for now
set -- response_l
if [ "$#" != '1' ]; then
	echo "Usage: rpc_memory_generated.sh <responseObjectName>" >&2
	exit 1
fi

responseObject="$1"

memory_information="$(emit_memory_information "${responseObject}" rai::node node.)"

mutexes=(
	$(
		echo "${memory_information}" | awk '/^MUTEX=/{ sub(/^MUTEX=/, ""); print }' | sort -u
	)
)
memory_information="$(echo "${memory_information}" | grep -v '^MUTEX=')"

if [ "${#mutexes[@]}" -gt 0 ]; then
	echo 'while (true)'
	echo '{'
	echo -n $'\t'"if (std::try_lock ("
	seperator=''
	for mutex in "${mutexes[@]}"; do
		if [[ "${mutex}" =~ "->" ]]; then
			continue;
		fi

		echo -n "${seperator}${mutex}"
		seperator=', '
	done
	echo ') != -1)'
	echo $'\t{'
	echo $'\t\tcontinue;'
	echo $'\t}'
	for mutex in "${mutexes[@]}"; do
		if ! [[ "${mutex}" =~ "->" ]]; then
			continue;
		fi

		unlock=''
		for unlockMutex in "${mutexes[@]}"; do
			if [[ "${unlockMutex}" =~ "->" ]]; then
				continue;
			fi

			unlock+=$'\t'"${unlockMutex}.unlock ();"$'\n'
		done
		for unlockMutex in "${mutexes[@]}"; do
			if ! [[ "${unlockMutex}" =~ "->" ]]; then
				continue;
			fi

			if [ "${unlockMutex}" = "${mutex}" ]; then
				break
			fi

			unlock+="$(guard_nullable "${unlockMutex}" "${unlockMutex}.unlock ();" $'\t')"$'\n'
		done

		guard_nullable "${mutex}" "if (!${mutex}.try_lock ())"$'\n{\n'"${unlock}"$'\tcontinue;\n}' $'\t'
	done
	echo $'\tbreak;'
	echo '}'
fi

echo "${memory_information}"

for mutex in "${mutexes[@]}"; do
	guard_nullable "${mutex}" "${mutex}.unlock ();"
done
