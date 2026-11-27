#!/bin/bash
set -euo pipefail

if (($# != 3)); then
    echo "::error::Usage: $0 <image> <arch> <output-dir>" >&2
    exit 1
fi
image=$1
arch=$2
output_dir=$3

image_ref=$(docker image inspect --format '{{.Id}}' "$image")
docker run --rm "$image_ref" --version

mkdir -p "$output_dir"
archive=$(mktemp "$output_dir/docker-save.XXXXXXXX")
trap 'rm -f "$archive"' EXIT
docker save --platform "linux/$arch" --output "$archive" "$image_ref"

# The saved config identifies the smoke-tested platform across Docker image stores
config=$(tar -xOf "$archive" manifest.json | jq -er 'select(length == 1) | .[0].Config')
image_id="sha256:$(tar -xOf "$archive" "$config" | sha256sum | cut -d ' ' -f 1)"
echo "Smoke-tested linux/$arch image $image_ref (config $image_id)"

# Preserve the Docker config bytes while compressing the layers
skopeo copy --format v2s2 --dest-compress-format gzip "docker-archive:$archive" "oci-archive:$output_dir/$arch.tar"
printf '%s\n' "$image_id" > "$output_dir/$arch.id"
