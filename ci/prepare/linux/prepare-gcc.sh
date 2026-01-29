#!/bin/bash
set -euox pipefail

# Install GCC 13 for C++23 support (std::unreachable, etc.)
apt-get install -yqq software-properties-common
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update -qq
apt-get install -yqq gcc-13 g++-13

# Set GCC 13 as default
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100