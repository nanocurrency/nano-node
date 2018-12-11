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
nano::vote_processor vote_processor
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
		'nano::alarm')
			cat << \_EOF_
std::priority_queue operations
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
			printName="$(echo "${name}" | sed 's@->@.@g;s@\.$@@')"

			mutex=''
			case "${type}" in
				'boost::multi_index_container'|'std::priority_queue'|'std::deque'|'std::unordered_map')
					# XXX:TODO: Find mutex name dynamically
					mutex="mutex"
					mutex="$(echo "${name}" | sed 's@\.\([^.]*\)\.$@@').${mutex}"
					output="${name}size ()";
					;;
			esac

			if [ -n "${output}" ]; then
				if [ -n "${mutex}" ]; then
					echo $'\t\t'"${mutex}.lock ();"
				fi
				echo $'\t\t'"response_l.put (\"${printName}\", ${output});"
				if [ -n "${mutex}" ]; then
					echo $'\t\t'"${mutex}.unlock ();"
				fi
			fi
		else
			echo "${output}"
		fi
	done
}

emit_memory_information nano::node node.
