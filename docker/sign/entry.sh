#!/bin/bash

if [ -z "${GPG_PRIVATE_KEY}" ]; then
    echo "GPG_PRIVATE_KEY not set"
    exit 1
fi

if [ -z "${GPG_SIGNER}" ]; then
    echo "GPG_SIGNER not set"
    exit 1
fi

echo "${GPG_PRIVATE_KEY}" | base64 -d | gpg2 --import --no-tty --yes

cat <<EOF > /root/.rpmmacros
%_signature gpg
%_gpg_name ${GPG_SIGNER}
%_gpg_path /root/.gnupg
%_gpgbin /usr/bin/gpg2
EOF


if [ "${1}" == "rpm-sign" ]; then
   if [ -z "${2}" ]; then
        echo "Usage: ${0} rpm-sign <base-dir-to-sign>"
        exit 1
    fi
    for a in `ls ${2}/*.rpm`; do
        rpm --addsign "${a}"
    done
    exit 0
elif [ "${1}" == "repo-update" ]; then
    if [ -z "${S3_ACCESS_KEY_ID}" ]; then
        echo "S3_ACCESS_KEY_ID not set"
        exit 1
    fi
    if [ -z "${S3_SECRET_ACCESS_KEY}" ]; then
        echo "S3_SECRET_ACCESS_KEY not set"
        exit 1
    fi
    if [ -z "${S3_REGION}" ]; then
        echo "Defaulting S3_REGION to us-east-2"
        export S3_REGION="us-east-2"
    fi
    if [ -z "${2}" ]; then
        echo "Usage: ${0} repo-update <s3 path>"
        exit 1
    fi
    mkrepo --s3-access-key-id ${S3_ACCESS_KEY_ID} \
        --s3-secret-access-key ${S3_SECRET_ACCESS_KEY} \
        --s3-public-read --s3-region ${S3_REGION} \
        ${2}

else
    echo "Usage: ${0} <rpm-sign|repo-update>"
    exit 1
fi