#!/bin/bash

set -o errexit
set -o nounset
set -o xtrace
OS=$(uname)
IS_RPM_DEPLOY="${LINUX_RPM:-0}"

if [[ "${BETA:-0}" -eq 1 ]]; then
    BUILD="beta"
elif [[ "${TEST:-0}" -eq 1 ]]; then
    BUILD="test"
else
    BUILD="live"
fi

if [[ "${GITHUB_REPOSITORY:-}" == "nanocurrency/nano-node" ]]; then
    DIRECTORY=$BUILD
else
    DIRECTORY="${S3_BUILD_DIRECTORY}/${BUILD}"
fi

if [[ "$OS" == 'Linux' && "$IS_RPM_DEPLOY" -eq "1" ]]; then
    RPMS=$(find ${GITHUB_WORKSPACE}/artifacts/RPMS -type f -name '*.rpm')
    SRPMS=$(find ${GITHUB_WORKSPACE}/artifacts/SRPMS -type f -name '*.src.rpm')
    for rpm in $RPMS; do
        sha256sum $rpm > "${GITHUB_WORKSPACE}/${rpm}.sha256"
        aws s3 cp "${rpm}" s3://repo.nano.org/$DIRECTORY/binaries/$(basename "${rpm}") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
        aws s3 cp "${GITHUB_WORKSPACE}/${rpm}.sha256" s3://repo.nano.org/$DIRECTORY/binaries/$(basename "${rpm}.sha256") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    done
    for srpm in $SRPMS; do
        sha256sum $srpm > "${GITHUB_WORKSPACE}/${srpm}.sha256"
        aws s3 cp "${srpm}" s3://repo.nano.org/$DIRECTORY/source/$(basename "${srpm}") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
        aws s3 cp "${GITHUB_WORKSPACE}/${srpm}.sha256" s3://repo.nano.org/$DIRECTORY/source/$(basename "${srpm}.sha256") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    done
elif [[ "$OS" == 'Linux' ]]; then
    sha256sum $GITHUB_WORKSPACE/build/nano-node-*-Linux.tar.bz2 >$GITHUB_WORKSPACE/nano-node-$TAG-Linux.tar.bz2.sha256
    sha256sum $GITHUB_WORKSPACE/build/nano-node-*-Linux.deb >$GITHUB_WORKSPACE/nano-node-$TAG-Linux.deb.sha256
    aws s3 cp $GITHUB_WORKSPACE/build/nano-node-*-Linux.tar.bz2 s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.tar.bz2 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/nano-node-$TAG-Linux.tar.bz2.sha256 s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.tar.bz2.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/build/nano-node-*-Linux.deb s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.deb --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/nano-node-$TAG-Linux.deb.sha256 s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.deb.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
else
    sha256sum $GITHUB_WORKSPACE/build/nano-node-*-Darwin.dmg >$GITHUB_WORKSPACE/build/nano-node-$TAG-Darwin.dmg.sha256
    aws s3 cp $GITHUB_WORKSPACE/build/nano-node-*-Darwin.dmg s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Darwin.dmg --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/build/nano-node-$TAG-Darwin.dmg.sha256 s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Darwin.dmg.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
fi
