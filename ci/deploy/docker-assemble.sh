#!/bin/bash
set -euo pipefail

if (($# != 3)); then
    echo "::error::Usage: $0 <archives-dir> <output-dir> <image:tag>" >&2
    exit 1
fi
archives_dir=$1
output_dir=$2
image=$3
layout="ocidir://$output_dir/node-index"

mkdir -p "$output_dir"
for arch in amd64 arm64; do
    regctl image import "$layout:$arch" "$archives_dir/$arch.tar"
done
regctl index create "$layout:release" --ref "$layout:amd64" --ref "$layout:arm64"

rows=()
for arch in amd64 arm64; do
    expected_id=$(<"$archives_dir/$arch.id")
    manifest_digest=$(regctl manifest head "$layout:release" --platform "linux/$arch" --require-digest)
    manifest=$(regctl manifest get "$layout@$manifest_digest" --format raw-body)
    imported_id=$(jq -er '.config.digest' <<< "$manifest")
    if [[ "$imported_id" != "$expected_id" ]]; then
        echo "::error::linux/$arch was smoke-tested as $expected_id but imported as $imported_id" >&2
        exit 1
    fi
    if ! jq -e 'all(.layers[].mediaType; test("[.+]gzip$"))' <<< "$manifest" >/dev/null; then
        echo "::error::Expected gzip-compressed image layers for linux/$arch" >&2
        exit 1
    fi
    printf -v row '| `linux/%s` | `%s` | `%s` |' "$arch" "$expected_id" "$manifest_digest"
    rows+=("$row")
done

index_digest=$(regctl manifest head --require-list "$layout:release")
{
    printf '### `%s`\n\n' "$image"
    echo '| Platform | Smoke-tested image ID (config digest) | Manifest digest |'
    echo '| --- | --- | --- |'
    printf '%s\n' "${rows[@]}"
    printf '\nIndex digest `%s`\n' "$index_digest"
} > "$output_dir/docker-summary.md"
