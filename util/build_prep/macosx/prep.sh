#! /usr/bin/env bash

# -----BEGIN COMMON.SH-----
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
