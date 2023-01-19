#!/usr/bin/env bash

###################################################################################################

source "$(dirname "$BASH_SOURCE")/impl/common.sh"
source "$(dirname "$BASH_SOURCE")/impl/code-inspector.sh"

###################################################################################################

set -o errexit
set -o nounset

code_inspect "${ROOTPATH:-.}"

echo "code-inspector check passed"

###################################################################################################
