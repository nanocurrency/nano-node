#! /usr/bin/env bash

# -----BEGIN COMMON.SH-----
scriptDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 1

function boost_version () {
	local boost_version
	boost_version="$(
		set -o pipefail
		echo $'#include <boost/version.hpp>\nBOOST_LIB_VERSION'  | cc -E - 2>/dev/null | tail -n 1 | sed 's@"@@g;s@_@.@g'
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
# -----END COMMON.SH-----

export DEBIAN_FRONTEND=noninteractive

set -euo pipefail

apt-get update --yes
apt-get --yes install git cmake ninja-build autotools-dev \
            build-essential g++ clang python-dev \
            libicu-dev libbz2-dev \
            locales wget curl apt-utils \
            lsb-release
apt-get --yes install xorg xvfb xauth xfonts-100dpi xfonts-75dpi xfonts-scalable xfonts-cyrillic
apt-get --yes install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler
apt remove --yes libboost-all-dev
apt autoremove --yes

if ! have boost; then
	"${scriptDirectory}/../bootstrap_boost.sh" -m
fi

if ! have boost; then
	echo "Unable to install boost" >&2

	exit 1
fi

if ! version_min 'boost --version' 1.65.999; then
	echo "boost version too low (1.66.0+ required)" >&2
	exit 1
fi

boost_dir="$(boost --install-prefix)"

echo "All verified."
echo ""
echo "Next steps:"
echo "    cmake -DBOOST_ROOT=${boost_dir} -DRAIBLOCKS_GUI=ON <dir>"
echo "    cpack -G \"TBZ2\""

exit 0
