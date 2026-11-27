#!/bin/bash
set -Eeuo pipefail

state=$(mktemp -d)
trap 'rm -rf "$state"' EXIT
trap 'echo "Publisher test failed at line $LINENO" >&2; cat "$state/log" >&2' ERR
export state
export GITHUB_OUTPUT="$state/outputs"
export index=ocidir://fixture:release
export expected_digest=sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
ghcr=ghcr.example/nano
hub=hub.example/nano
export GHCR_REPOSITORY=$ghcr HUB_REPOSITORY=$hub CI_TAG=V1
mkdir -p "$state/ghcr.example" "$state/hub.example"

cat > "$state/regctl" <<'REGCTL'
#!/bin/bash
set -euo pipefail
target=${!#}
case "$1 $2" in
    'manifest get')
        echo '{"manifests":[{"platform":{"os":"linux","architecture":"amd64"}},{"platform":{"os":"linux","architecture":"arm64"}}]}'
        ;;
    'manifest head')
        if [[ "$target" == "$index" ]]; then
            echo "$expected_digest"
            exit 0
        fi
        echo "level=WARN msg=\"Registry warning message\" warning=\"previously not found [http 404]: $target\"" >&2
        if [[ "$target" == "${denied_target:-}" ]]; then
            echo "failed to request manifest head $target: request failed: unauthorized [http 403]:" >&2
            exit 1
        elif [[ ! -f "$state/$target" ]]; then
            echo "failed to request manifest head $target: request failed: not found [http 404]:" >&2
            exit 1
        fi
        cat "$state/$target"
        ;;
    'image copy')
        if [[ "$target" == "${failed_copy:-}" ]]; then
            echo "Upload failed for $target" >&2
            exit 1
        fi
        echo "$target" >> "$state/writes"
        echo "$expected_digest" > "$state/$target"
        ;;
    *) exit 2 ;;
esac
REGCTL
chmod +x "$state/regctl"
export PATH="$state:$PATH"

publish() {
    : > "$state/writes"
    : > "$GITHUB_OUTPUT"
    bash ci/deploy/docker-push.sh "$index" "$@" > "$state/log" 2>&1
}

assert_no_publication() {
    test ! -s "$state/writes"
    if grep -Eq '^(ghcr|hub)_digest=' "$GITHUB_OUTPUT"; then
        echo "Publisher emitted an attestation digest after a failed registry check" >&2
        return 1
    fi
}

# A matching version with a warning allows the retry to fill the missing registry
echo "$expected_digest" > "$state/$ghcr:V1"
publish "$ghcr:V1" "$hub:V1"
grep -Fq "previously not found [http 404]: $ghcr:V1" "$state/log"
[[ $(<"$state/$hub:V1") == "$expected_digest" ]]
[[ $(<"$state/writes") == "$hub@$expected_digest"$'\n'"$hub:V1" ]]

# A Hub conflict blocks a pending tag and the attestation for a matching GHCR version
export CI_TAG=V2
echo "$expected_digest" > "$state/$ghcr:$CI_TAG"
echo sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb > "$state/$hub:$CI_TAG"
if publish "$ghcr:$CI_TAG" "$ghcr:latest" "$hub:$CI_TAG"; then
    echo "Expected a conflicting version to fail publication" >&2
    exit 1
fi
grep -Fq "$hub:$CI_TAG already exists" "$state/log"
assert_no_publication

# A warning mentioning a 404 cannot turn a failed lookup into permission to write
export CI_TAG=V3 denied_target="$hub:V3"
echo "$expected_digest" > "$state/$ghcr:$CI_TAG"
if publish "$ghcr:$CI_TAG" "$hub:$CI_TAG"; then
    echo "Expected a denied lookup to fail publication" >&2
    exit 1
fi
grep -Fq 'unauthorized [http 403]' "$state/log"
assert_no_publication
unset denied_target

# Report only the verified GHCR version when the Hub version upload fails
export CI_TAG=V4 failed_copy="$hub:V4"
if publish "$ghcr:$CI_TAG" "$ghcr:latest" "$hub:$CI_TAG"; then
    echo "Expected the Hub version upload to fail" >&2
    exit 1
fi
grep -Fq "Upload failed for $hub:$CI_TAG" "$state/log"
[[ $(<"$state/$ghcr:$CI_TAG") == "$expected_digest" && ! -f "$state/$hub:$CI_TAG" ]]
[[ -f "$state/$hub@$expected_digest" ]]
[[ $(<"$GITHUB_OUTPUT") == "ghcr_digest=$expected_digest" ]]

# The retry verifies the existing GHCR version and the newly published Hub version
unset failed_copy
publish "$ghcr:$CI_TAG" "$ghcr:latest" "$hub:$CI_TAG"
grep -Fxq "ghcr_digest=$expected_digest" "$GITHUB_OUTPUT"
grep -Fxq "hub_digest=$expected_digest" "$GITHUB_OUTPUT"
[[ $(<"$state/writes") == "$hub@$expected_digest"$'\n'"$hub:$CI_TAG" ]]

# Both version digests remain available if a later latest upload fails
export failed_copy="$hub:latest"
if publish "$ghcr:$CI_TAG" "$hub:$CI_TAG" "$hub:latest"; then
    echo "Expected the Hub latest upload to fail" >&2
    exit 1
fi
grep -Fq "Upload failed for $hub:latest" "$state/log"
grep -Fxq "ghcr_digest=$expected_digest" "$GITHUB_OUTPUT"
grep -Fxq "hub_digest=$expected_digest" "$GITHUB_OUTPUT"

# An attestation-only retry needs no registry tag writes
publish "$ghcr:$CI_TAG" "$hub:$CI_TAG"
grep -Fxq "ghcr_digest=$expected_digest" "$GITHUB_OUTPUT"
grep -Fxq "hub_digest=$expected_digest" "$GITHUB_OUTPUT"
[[ ! -s "$state/writes" ]]

# A fork without Hub credentials publishes the version and latest only to its lowercase GHCR path
unset failed_copy
export GITHUB_REPOSITORY=Example/Nano-Node DOCKER_REGISTRY=example CI_TAG=V5
export NETWORK=live IMAGE_NAME=nano-nightly
ghcr_namespace=ghcr.io/example/nano-node
hub_namespace=docker.io/example
mkdir -p "$state/$ghcr_namespace" "$state/$hub_namespace"
DOCKER_PASSWORD='' publish --workflow-tags
grep -Fxq "ghcr_repository=$ghcr_namespace/$IMAGE_NAME" "$GITHUB_OUTPUT"
grep -Fxq "ghcr_digest=$expected_digest" "$GITHUB_OUTPUT"
[[ -f "$state/$ghcr_namespace/$IMAGE_NAME:$CI_TAG" && -f "$state/$ghcr_namespace/$IMAGE_NAME:latest" ]]
[[ ! -e "$state/$hub_namespace/$IMAGE_NAME:$CI_TAG" && $(<"$GITHUB_OUTPUT") != *hub_digest=* ]]

# Hub gets the live version without latest
DOCKER_PASSWORD=test publish --workflow-tags
grep -Fxq "hub_repository=$hub_namespace/$IMAGE_NAME" "$GITHUB_OUTPUT"
grep -Fxq "hub_digest=$expected_digest" "$GITHUB_OUTPUT"
[[ -f "$state/$hub_namespace/$IMAGE_NAME:$CI_TAG" && ! -e "$state/$hub_namespace/$IMAGE_NAME:latest" ]]

# A failed beta upload leaves GHCR ready to attest and a retry fills both Hub tags
export NETWORK=beta IMAGE_NAME=nano-beta DOCKER_REGISTRY=''
hub_namespace=docker.io/nanocurrency
mkdir -p "$state/$hub_namespace"
if DOCKER_PASSWORD=test failed_copy="$hub_namespace/$IMAGE_NAME:$CI_TAG" publish --workflow-tags; then
    echo "Expected the Hub version upload to fail" >&2
    exit 1
fi
grep -Fxq "ghcr_repository=$ghcr_namespace/$IMAGE_NAME" "$GITHUB_OUTPUT"
grep -Fxq "ghcr_digest=$expected_digest" "$GITHUB_OUTPUT"
[[ $(<"$GITHUB_OUTPUT") != *hub_digest=* ]]
DOCKER_PASSWORD=test publish --workflow-tags
grep -Fxq "hub_repository=$hub_namespace/$IMAGE_NAME" "$GITHUB_OUTPUT"
grep -Fxq "hub_digest=$expected_digest" "$GITHUB_OUTPUT"
[[ -f "$state/$hub_namespace/$IMAGE_NAME:$CI_TAG" && -f "$state/$hub_namespace/$IMAGE_NAME:latest" ]]

# Both version tags match, but the final Hub latest lookup must also pass before attestation
if DOCKER_PASSWORD=test denied_target="$hub_namespace/$IMAGE_NAME:latest" publish --workflow-tags; then
    echo "Expected a denied Hub latest lookup to fail publication" >&2
    exit 1
fi
grep -Fq 'unauthorized [http 403]' "$state/log"
assert_no_publication
