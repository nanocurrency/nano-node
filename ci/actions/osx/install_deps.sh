#!/bin/bash

brew update
brew install coreutils
brew cask install xquartz
if [[ ${TEST-0} -eq 1 ]]; then
    brew install rocksdb;
else
    util/build_prep/fetch_rocksdb.sh
fi
sudo util/build_prep/fetch_boost.sh 
util/build_prep/macosx/build_qt.sh
