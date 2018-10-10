#! /usr/bin/env bash

# -----BEGIN COMMON.SH-----
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
	bootstrap_boost -m -k
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
