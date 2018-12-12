#! /usr/bin/env bash

# XXX:TODO: Write a tool that implements this
function emit_members () {
	local class

	class="$1"

	case "${class}" in
		'nano::node')
			cat << \_EOF_
nano::gap_cache gap_cache
nano::ledger ledger
nano::active_transactions active
nano::network network
nano::bootstrap_initiator bootstrap_initiator
nano::bootstrap_listener bootstrap
nano::peer_container peers
nano::node_observers observers
nano::signature_checker checker
nano::vote_processor vote_processor
nano::rep_crawler rep_crawler
nano::block_processor block_processor
nano::block_arrival block_arrival
nano::online_reps online_reps
nano::stat stats
nano::block_uniquer block_uniquer
nano::vote_uniquer vote_uniquer
nano::alarm alarm
_EOF_
			;;
		'nano::gap_cache')
			cat << \_EOF_
boost::multi_index_container blocks
_EOF_
			;;
		'nano::active_transactions')
			cat << \_EOF_
boost::multi_index_container roots
std::unordered_map blocks
std::deque confirmed
_EOF_
			;;
		'nano::bootstrap_listener')
			cat << \_EOF_
std::unordered_map connections
_EOF_
			;;
		'nano::bootstrap_initiator')
			cat << \_EOF_
nano::bootstrap_attempt *attempt
_EOF_
			;;
		'nano::bootstrap_attempt')
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
		'nano::alarm')
			cat << \_EOF_
std::priority_queue operations
_EOF_
			;;
		'nano::block_processor')
			cat << \_EOF_
std::deque state_blocks
std::deque blocks
std::unordered_set blocks_hashes
std::deque forced
_EOF_
			;;
		'nano::vote_processor')
			cat << \_EOF_
std::deque votes
std::unordered_set representatives_1
std::unordered_set representatives_2
std::unordered_set representatives_3
_EOF_
			;;
	esac
}

function emit_memory_information () {
	local startAtType startAtName
	local type name printName
	local output

	startAtType="$1"
	startAtName="$2"

	emit_members "${startAtType}" | while read -r type name; do
		case "${name}" in
			'*'*)
				name="${startAtName}${name:1}->"
				;;
			*)
				name="${startAtName}${name}."
				;;
		esac

		output="$(emit_memory_information "${type}" "${name}")"

		if [ -z "${output}" ]; then
			printName="$(echo "${name}" | sed 's@ *()@_function@;s@->@.@g;s@\.$@@')"

			mutex=''
			case "${type}" in
				'boost::multi_index_container'|'std::priority_queue'|'std::deque'|'std::unordered_map'|'std::vector'|'std::unordered_set')
					# XXX:TODO: Find mutex name dynamically
					case "${printName}" in
						*_function)
							;;
						*.lazy_*)
							# XXX:TODO: Disabled mutex until we do something saner
							#mutex="lazy_mutex"
							;;
						*)
							mutex="mutex"
							;;
					esac

					output="${name}size ()";
					;;
			esac

			if [ -n "${output}" ]; then
				if [ -n "${mutex}" ]; then
					mutex="$(echo "${name}" | sed 's@\.\([^.]*\)\.$@@').${mutex}"
					echo "MUTEX=${mutex}"
				fi

				nullCheck="${output}"
				nullable="$(
					while [[ "${nullCheck}" =~ '->' ]]; do
						nullCheck="$(echo "${nullCheck}" | sed 's@->[^-]*$@@')"
						echo "${nullCheck}"
					done | tac
				)"

				addTabs=""
				while IFS='' read -r nullCheck; do
					if [ -z "${nullCheck}" ]; then
						continue
					fi

					echo "${addTabs}if (${nullCheck})"
					echo "${addTabs}{"
					addTabs+=$'\t'
				done <<<"${nullable}"

				echo "${addTabs}response_l.put (\"${printName}\", ${output});"

				while IFS='' read -r nullCheck; do
					if [ -z "${nullCheck}" ]; then
						continue
					fi

					addTabs="${addTabs:1}"
					echo "${addTabs}}"
				done <<<"${nullable}"
			fi
		else
			echo "${output}"
		fi
	done
}

memory_information="$(emit_memory_information nano::node node.)"

mutexes=(
	$(
		echo "${memory_information}" | awk '/^MUTEX=/{ sub(/^MUTEX=/, ""); print }' | sort -u
	)
)
memory_information="$(echo "${memory_information}" | grep -v '^MUTEX=')"

if [ "${#mutexes[@]}" -gt 0 ]; then
	echo -n "std::lock ("
	seperator=''
	for mutex in "${mutexes[@]}"; do
		echo -n "${seperator}${mutex}"
		seperator=', '
	done
	echo ');'
fi

echo "${memory_information}"

for mutex in "${mutexes[@]}"; do
	echo "${mutex}.unlock ();"
done
