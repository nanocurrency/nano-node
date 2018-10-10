KEEP_AROUND_DIRECTORY="${HOME:-/dev/null}/.cache/nanocurrency-build"
scriptDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 1

function boost_version () {
	local boost_version
	boost_version="$(
		set -o pipefail
		echo $'#include <boost/version.hpp>\nBOOST_LIB_VERSION'  | cc -I/usr/local/boost/include -E - 2>/dev/null | tail -n 1 | sed 's@"@@g;s@_@.@g'
	)" || boost_version=''

	echo "${boost_version}"
}

function check_create_boost () {
	local boost_version
	boost_version="$(boost_version)"

	if [ -n "${boost_version}" ]; then
		function boost () {
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
					echo '#include <boost/version.hpp>' | cc -v -E - 2>/dev/null | grep '/version.hpp' | sed 's@^[^"]*"@@;s@/version\.hpp".*$@@'
					return 0
					;;
			esac

			return 1
		}
	fi
}

function have () {
	local program

	program="$1"

	check_create_boost

	type -t "${program}" >/dev/null 2>/dev/null
}

function version_min () {
	local version_command below_min_version
	local check_version

	version_command="$1"
	below_min_version="$2"

	check_version="$(
		(
			eval "${version_command}" | awk '{ print $NF }' | grep '^[0-9]'
			echo "${below_min_version}"
		) | sort -rV | head -n 1
	)"

	if [ "${check_version}" = "${below_min_version}" ]; then
		return 1
	fi

	return 0
}
