#! /usr/bin/env bash

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

# XXX:TODO: Use common.sh
boost_dir=/usr/local/boost

echo "All verified."
echo ""
echo "Next steps:"
echo "    cmake -DBOOST_ROOT=${boost_dir} -DRAIBLOCKS_GUI=ON <dir>"
echo "    cpack -G \"TBZ2\""

exit 0
