#!/bin/bash

export PATH="$PATH:/c/Program Files (x86)/Microsoft Visual Studio/Installer/"
export MSVC=$(vswhere -latest -products "*" -requires Microsoft.Component.MSBuild -property installationPath)
export MSVC=$(echo /$MSVC | sed -e 's/\\/\//g' -e 's/://')
export PATH="$PATH:$MSVC/MSBuild/15.0/Bin/"

NANO_ROOT=`PWD`
mkdir build_win
cd build_win

cmake \
    -G'Visual Studio 15 2017 Win64' \
    -DACTIVE_NETWORK=nano_test_network \
    -DNANO_TEST=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DBOOST_ROOT="~/platform/boost/" \
    -DIPHLPAPI_LIBRARY="/c/Program Files (x86)/Windows Kits/10/Lib/10.0.17134.0/um/x64/iphlpapi.Lib" \
    -DWINSOCK2_LIBRARY="/c/Program Files (x86)/Windows Kits/10/Lib/10.0.17134.0/um/x64/WS2_32.Lib" \
    -Dgtest_force_shared_crt=true \
    $NANO_ROOT

cmd.exe /C "MSBuild.exe \"nano/core_test/core_test.vcxproj\" /m"
