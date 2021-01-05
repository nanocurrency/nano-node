KEEP_AROUND_DIRECTORY="${HOME:-/dev/null}/.cache/nanocurrency-build"

function _cpp() {
	"${CC:-cc}" -I"${BOOST_ROOT:-/usr/local/boost}"/include -E "$@"
}

function boost_version() {
	local boost_version
	boost_version="$(
		set -o pipefail
		echo $'#include <boost/version.hpp>\nBOOST_LIB_VERSION' | _cpp - 2>/dev/null | tail -n 1 | sed 's@"@@g;s@_@.@g'
	)" || boost_version=''

	echo "${boost_version}"
}

function check_create_boost() {
	local boost_version
	boost_version="$(boost_version)"

	if [ -n "${boost_version}" ]; then
		function boost() {
			local arg
			local version

			arg="$1"

			version="$(boost_version)"
			if [ -z "${version}" ]; then
				return 1
			fi

			case "${arg}" in
			'')
				return 0
				;;
			'--version')
				echo "${version}"
				return 0
				;;
			'--install-prefix')
				echo '#include <boost/version.hpp>' | _cpp -v - 2>/dev/null | grep '/version.hpp' | sed 's@^[^"]*"@@;s@/boost/version\.hpp".*$@@'
				return 0
				;;
			esac

			return 1
		}
	fi
}

function have() {
	local program

	program="$1"

	check_create_boost

	type -t "${program}" >/dev/null 2>/dev/null
}

function version_min() {
	local version_command below_min_version above_max_version
	local detected_version check_version

	version_command="$1"
	below_min_version="${2:-0}"
	above_max_version="${3:-2147483648}"

	detected_version="$(eval "${version_command}" | awk '{ print $NF }' | grep '^[0-9]' | head -n 1)"

	check_version="$(
		(
			echo "${below_min_version}"
			echo "${detected_version}"
			echo "${above_max_version}"
		) | sort -rV | tail -n 2 | head -n 1
	)"

	if [ "${check_version}" != "${detected_version}" ]; then
		return 1
	fi

	return 0
}

function wrap_compilers() {
	local tool tool_check tool_add tool_wrapper_file
	local prep_common_workdir_bin

	if [ -z "${wrap_compilers_add_options[*]}" ]; then
		return
	fi

	prep_common_workdir_bin="${prep_common_workdir}/bin"
	mkdir -p "${prep_common_workdir_bin}" || return 1

	tool_add=()
	for tool in cc c++ clang clang++ gcc g++; do
		tool_check="$(type -f "${tool}")" || tool_check=''
		if [ -n "${tool_check}" ]; then
			tool_add+=("${tool}")
		fi
	done

	for tool in "${tool_add[@]}"; do
		tool_wrapper_file="${prep_common_workdir_bin}/${tool}"
		if [ -e "${tool_wrapper_file}" ]; then
			continue
		fi

		(
			echo '#! /usr/bin/env bash'
			echo ''
			set | grep '^tool='
			set | grep '^wrap_compilers_add_options='
			set | grep '^prep_common_workdir='
			echo ''
			cat <<\_EOF_

NEW_PATH='/dev/null'
while read -r -d ':' element; do
	case "${element}" in
		"${prep_common_workdir}"|"${prep_common_workdir}"/*)
			continue
			;;
	esac

	NEW_PATH="${NEW_PATH}:${element}"
done <<<"${PATH}"
PATH="${NEW_PATH}"
export PATH

exec "${tool}" "${wrap_compilers_add_options[@]}" "$@"
_EOF_
		) >"${tool_wrapper_file}"

		chmod +x "${tool_wrapper_file}"
	done

	PATH="${prep_common_workdir_bin}:${PATH}"
	export PATH
}

function common_cleanup() {
	if [ -n "${prep_common_workdir}" ]; then
		rm -rf "${prep_common_workdir}"
	fi
}

trap common_cleanup EXIT

prep_common_workdir="$(mktemp -d)"
