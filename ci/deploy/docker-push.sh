#!/bin/bash
set -euo pipefail

# Publish one complete, locally assembled index under new registry tags.
if (($# < 2)) || [[ $2 == --workflow-tags && $# -ne 2 ]]; then
    echo "::error::Usage: $0 <local-index> {--workflow-tags | <repository:tag>...}" >&2
    exit 1
fi
source_index=$1
shift

if [[ $1 == --workflow-tags ]]; then
    repository=$(tr '[:upper:]' '[:lower:]' <<< "${GITHUB_REPOSITORY:?}")
    GHCR_REPOSITORY="ghcr.io/$repository/${IMAGE_NAME:?}"
    HUB_REPOSITORY="docker.io/${DOCKER_REGISTRY:-nanocurrency}/$IMAGE_NAME"
    targets=("$GHCR_REPOSITORY:${CI_TAG:?}" "$GHCR_REPOSITORY:latest")
    if [[ -n "${DOCKER_PASSWORD:-}" ]]; then
        targets+=("$HUB_REPOSITORY:$CI_TAG")
        if [[ "${NETWORK:?}" == beta ]]; then
            targets+=("$HUB_REPOSITORY:latest")
        fi
    fi
    if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
        printf 'ghcr_repository=%s\nhub_repository=%s\n' "$GHCR_REPOSITORY" "$HUB_REPOSITORY" >> "$GITHUB_OUTPUT"
    fi
else
    targets=("$@")
fi

digest=$(regctl manifest head --require-list "$source_index")
if ! regctl manifest get "$source_index" --format raw-body | jq -e '
    [.manifests[] | [.platform.os, .platform.architecture]] | sort == [["linux", "amd64"], ["linux", "arm64"]]
' >/dev/null; then
    echo "::error::Expected linux/amd64 and linux/arm64 in $source_index" >&2
    exit 1
fi

lookup_stderr=$(mktemp)
trap 'rm -f "$lookup_stderr"' EXIT

# Print the tag only if the registry confirms it is absent. Only a 404 permits creation.
new_tag() {
    local target=$1 existing
    if existing=$(regctl manifest head --require-digest "$target" 2>"$lookup_stderr"); then
        cat "$lookup_stderr" >&2
        if [[ "$existing" != "$digest" && "$target" != *:latest ]]; then
            echo "::error::$target already exists at $existing (this build is $digest)" >&2
            return 1
        fi
        echo "Keeping existing $target at $existing" >&2
    elif grep -Eq '^failed to (request manifest head|get manifest) [^ ]+: request failed: not found \[http 404\]:' "$lookup_stderr"; then
        echo "$target"
    else
        echo "::error::Cannot check $target: $(<"$lookup_stderr")" >&2
        return 1
    fi
}

# Record verified version tags even if a later upload fails
record_version_digest() {
    [[ -n "${GITHUB_OUTPUT:-}" ]] || return 0
    case "$1" in
        "${GHCR_REPOSITORY:-}:${CI_TAG:-}") echo "ghcr_digest=$digest" >> "$GITHUB_OUTPUT" ;;
        "${HUB_REPOSITORY:-}:${CI_TAG:-}") echo "hub_digest=$digest" >> "$GITHUB_OUTPUT" ;;
    esac
}

# Check every destination for a conflict before publishing to any of them.
pending=()
existing_targets=()
for target in "${targets[@]}"; do
    candidate=$(new_tag "$target")
    if [[ -n "$candidate" ]]; then
        pending+=("$target")
    else
        existing_targets+=("$target")
    fi
done

# Enable attestations for existing versions only after every destination check passes
for ((i = 0; i < ${#existing_targets[@]}; i++)); do
    record_version_digest "${existing_targets[i]}"
done

for ((i = 0; i < ${#pending[@]}; i++)); do
    target=${pending[i]}
    # Copy both images and the index by digest first. Make the tag visible last.
    destination="${target%:*}@$digest"
    regctl image copy "$source_index" "$destination"
    candidate=$(new_tag "$target")
    if [[ -n "$candidate" ]]; then
        regctl image copy "$destination" "$target"
        published=$(regctl manifest head --require-list --require-digest "$target")
        if [[ "$published" != "$digest" ]]; then
            echo "::error::$target points to $published, expected $digest" >&2
            exit 1
        fi
        echo "::notice::Published $target at $digest"
    fi
    record_version_digest "$target"
done

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "digest=$digest" >>"$GITHUB_OUTPUT"
fi
