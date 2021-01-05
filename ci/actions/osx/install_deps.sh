#!/bin/bash

brew update
brew install coreutils
brew cask install xquartz
sudo util/build_prep/fetch_boost.sh
util/build_prep/macosx/build_qt.sh
