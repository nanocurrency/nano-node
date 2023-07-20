#!/bin/bash

set -e
set -x

source "$(dirname "$BASH_SOURCE")/docker-impl/docker-common.sh"

ghcr_deploy