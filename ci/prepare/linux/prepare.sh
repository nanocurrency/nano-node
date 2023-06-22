#!/bin/bash
set -euox pipefail

COMPILER=${COMPILER:-gcc}

echo "Compiler: '${COMPILER}'"

# Common dependencies needed for building & testing
apt-get update -qq

DEBIAN_FRONTEND=noninteractive apt-get install -yqq \
build-essential \
g++ \
wget \
python3 \
zlib1g-dev \
cmake \
git \
qtbase5-dev \
qtchooser \
qt5-qmake \
qtbase5-dev-tools \
valgrind \
xorg xvfb xauth xfonts-100dpi xfonts-75dpi xfonts-scalable xfonts-cyrillic

# Compiler specific setup
$(dirname "$BASH_SOURCE")/prepare-${COMPILER}.sh