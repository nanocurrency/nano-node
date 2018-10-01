#! /usr/bin/env bash

# -----BEGIN COMMON.SH-----
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

# Ensure we have a new enough CMake
if ! have cmake; then
	brew install cmake || exit 1
	brew link cmake || exit 1
fi

if ! have cmake; then
	echo "Unable to install cmake" >&2

	exit 1
fi

if ! version_min 'cmake --version' 3.3.999; then
	echo "cmake version too low (3.4+ required)" >&2

	exit 1
fi

# Ensure we have a new enough Boost
if ! have boost; then
	brew install boost --without-single || exit 1
	brew link boost || exit 1
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

# Ensure we have a new enough Qt5
PATH="${PATH}:/usr/local/opt/qt/bin"
export PATH
if ! have qtpaths; then
	brew install qt5 || exit 1
fi

if ! have qtpaths; then
	echo "Unable to install qt5" >&2

	exit 1
fi

if ! version_min 'qtpaths --qt-version' 4.999; then
	echo "qt5 version too low (5.0+ required)" >&2

	exit 1
fi
qt5_dir="$(qtpaths --install-prefix)"

echo "All verified."
echo ""
echo "Next steps:"
echo "    cmake -DBOOST_ROOT=${boost_dir} -DRAIBLOCKS_GUI=ON -DQt5_DIR=${qt5_dir}/lib/cmake/Qt5 <dir>"
echo "    cpack -G \"DragNDrop\""

exit 0
