#!/bin/bash

export DEBIAN_FRONTEND=noninteractive

set -euo pipefail

apt-get update
apt-get --yes --force-yes install git cmake ninja-build autotools-dev \
            build-essential g++ clang python-dev \
            libicu-dev libbz2-dev libboost-all-dev \
            locales wget curl apt-utils \
            lsb-release
apt-get --yes --force-yes install xorg xvfb xauth xfonts-100dpi xfonts-75dpi xfonts-scalable xfonts-cyrillic
apt-get --yes --force-yes install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler
