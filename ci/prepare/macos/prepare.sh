#!/bin/bash
set -euox pipefail

brew update
brew install coreutils

brew install qt@5
brew link qt@5

# Workaround: https://github.com/Homebrew/homebrew-core/issues/8392
echo "$(brew --prefix qt5)/bin" >> $GITHUB_PATH