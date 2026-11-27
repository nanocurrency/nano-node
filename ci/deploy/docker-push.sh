#!/bin/bash
set -euo pipefail

# Publish one complete, locally assembled index under new registry tags.
if (($# < 2)); then
    echo "::error::Usage: $0 <local-index> <repository:tag>..." >&2
    exit 1
fi
source_index=$1
shift

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

# Check every destination for a conflict before publishing to any of them.
pending=()
for target in "$@"; do
    candidate=$(new_tag "$target")
    if [[ -n "$candidate" ]]; then
        pending+=("$target")
    fi
done

for target in "${pending[@]}"; do
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
done

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "digest=$digest" >>"$GITHUB_OUTPUT"
fi
