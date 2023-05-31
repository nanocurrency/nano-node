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
        SHA=$(sha256sum ${rpm})
        echo "${rpm}: $SHA"
        echo $SHA > "${rpm}.sha256"

        aws s3 cp "${rpm}" s3://repo.nano.org/$DIRECTORY/binaries/$(basename "${rpm}") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
        aws s3 cp "${rpm}.sha256" s3://repo.nano.org/$DIRECTORY/binaries/$(basename "${rpm}.sha256") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    done

    for srpm in $SRPMS; do
        SHA=$(sha256sum ${srpm})
        echo "${srpm}: $SHA"
        echo $SHA > "${srpm}.sha256"

        aws s3 cp "${srpm}" s3://repo.nano.org/$DIRECTORY/source/$(basename "${srpm}") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
        aws s3 cp "${srpm}.sha256" s3://repo.nano.org/$DIRECTORY/source/$(basename "${srpm}.sha256") --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    done
elif [[ "$OS" == 'Linux' ]]; then
    TAR_PATH=$GITHUB_WORKSPACE/build/nano-node-*-Linux.tar.bz2
    DEB_PATH=$GITHUB_WORKSPACE/build/nano-node-*-Linux.deb

    SHA=$(sha256sum $TAR_PATH)
    echo "${TAR_PATH}: $SHA"
    echo $SHA > "${TAR_PATH}.sha256"

    SHA=$(sha256sum $DEB_PATH)
    echo "${DEB_PATH}: $SHA"
    echo $SHA > "${DEB_PATH}.sha256"

    aws s3 cp "${TAR_PATH}" s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.tar.bz2 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp "${TAR_PATH}.sha256" s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.tar.bz2.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp "${DEB_PATH}" s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.deb --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp "${DEB_PATH}.sha256" s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Linux.deb.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
else
    DMG_PATH=$GITHUB_WORKSPACE/build/nano-node-*-Darwin.dmg

    SHA=$(sha256sum $DMG_PATH)
    echo "${DMG_PATH}: $SHA"
    echo $SHA > "${DMG_PATH}.sha256"
    
    aws s3 cp "${DMG_PATH}" s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Darwin.dmg --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp "${DMG_PATH}.sha256" s3://repo.nano.org/$DIRECTORY/binaries/nano-node-$TAG-Darwin.dmg.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
fi
