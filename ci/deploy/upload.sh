#!/bin/bash
set -euo pipefail

# Publishes release packages to the download bucket, each with a .sha256 beside it.
# Usage: ci/deploy/upload.sh <file>...
# NETWORK picks the beta or live directory, S3_BUCKET_NAME and S3_BUILD_DIRECTORY the bucket
# and an optional prefix inside it, and the aws cli takes its credentials from the environment.

die() {
    echo "::error::$*" >&2
    exit 1
}

if (($# == 0)); then
    die "Usage: $0 <file>..."
fi

case "${NETWORK:-}" in
    BETA) network=beta ;;
    LIVE) network=live ;;
    *)
        die "NETWORK must be BETA or LIVE, got '${NETWORK:-}'"
        ;;
esac

bucket=${S3_BUCKET_NAME:-repo.nano.org}
directory=${S3_BUILD_DIRECTORY:+${S3_BUILD_DIRECTORY}/}${network}/binaries
destination="s3://${bucket}/${directory}"

echo "Publishing $# file(s) to ${destination}"

# Written from inside the file's directory so the checksum names the bare file and sha256sum -c accepts it
write_checksum() {
    local file=$1
    (cd "$(dirname "${file}")" && sha256sum "$(basename "${file}")" >"$(basename "${file}").sha256")
}

publish() {
    local file=$1
    aws s3 cp "${file}" "${destination}/$(basename "${file}")" --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
}

for file in "$@"; do
    publish "${file}"
    # The attestation bundle is a signature itself and gets no checksum
    if [[ "${file}" != *.sigstore.json ]]; then
        write_checksum "${file}"
        echo "::notice::$(cat "${file}.sha256")"
        publish "${file}.sha256"
    fi
done
