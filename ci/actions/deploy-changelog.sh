#!/bin/bash

set -o errexit
set -o nounset
set -o xtrace

TAG="$1"

if [[ -z "$TAG" ]]; then
  echo "The TAG argument is required"
  exit 1
fi

BUILD="changelogs"
if [[ "${GITHUB_REPOSITORY:-}" == "nanocurrency/nano-node" ]]; then
    DIRECTORY=$BUILD
else
    DIRECTORY="${S3_BUILD_DIRECTORY}/${BUILD}"
fi

aws s3 cp $GITHUB_WORKSPACE/artifacts/CHANGELOG.md s3://repo.nano.org/$DIRECTORY/CHANGELOG-${TAG}.md --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
