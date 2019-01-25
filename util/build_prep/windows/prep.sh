#!/bin/bash

mkdir -p ~/platform/boost/lib
nuget install ./util/build_prep/windows/packages.config -ExcludeVersion -OutputDirectory ~/tmp
cp ~/tmp/boost_*/lib/native/libboost*.lib ~/platform/boost/lib
mv ~/tmp/boost/lib/native/include/* ~/platform/boost/
