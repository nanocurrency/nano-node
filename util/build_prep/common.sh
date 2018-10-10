KEEP_AROUND_DIRECTORY="${HOME:-/dev/null}/.cache/nanocurrency-build"
scriptDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 1

function _cpp () {
	"${CC:-cc}" -I"${BOOST_ROOT:-/usr/local/boost}"/include -E "$@"
}

function boost_version () {
	local boost_version
	boost_version="$(
		set -o pipefail
		echo $'#include <boost/version.hpp>\nBOOST_LIB_VERSION'  | _cpp - 2>/dev/null | tail -n 1 | sed 's@"@@g;s@_@.@g'
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
					echo '#include <boost/version.hpp>' | _cpp -v - 2>/dev/null | grep '/version.hpp' | sed 's@^[^"]*"@@;s@/boost/version\.hpp".*$@@'
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
	local version_command below_min_version above_max_version
	local detected_version check_version

	version_command="$1"
	below_min_version="$2"
	above_max_version="$3"

	detected_version="$(eval "${version_command}" | awk '{ print $NF }' | grep '^[0-9]' | head -n 1)"

	check_version="$(
		(
			echo "${below_min_version:-0}"
			echo "${detected_version}"
			echo "${above_max_version:-2147483648}"
		) | sort -rV | tail -n 2 | head -n 1
	)"

	if [ "${check_version}" != "${detected_version}" ]; then
		return 1
	fi

	return 0
}
