#!/bin/bash

brew update
brew install coreutils
brew cask install xquartz
brew install openssl@1.1
sudo util/build_prep/fetch_boost.sh
util/build_prep/macosx/build_qt.sh
