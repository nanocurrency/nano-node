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

# A matching version with a warning allows the retry to fill the missing registry
echo "$expected_digest" > "$state/$ghcr:V1"
publish "$ghcr:V1" "$hub:V1"
grep -Fq "previously not found [http 404]: $ghcr:V1" "$state/log"
[[ $(<"$state/$hub:V1") == "$expected_digest" ]]
[[ $(<"$state/writes") == "$hub@$expected_digest"$'\n'"$hub:V1" ]]

# A conflicting version aborts the preflight before a missing tag is written
echo sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb > "$state/$hub:V2"
if publish "$ghcr:V2" "$hub:V2"; then
    echo "Expected a conflicting version to fail publication" >&2
    exit 1
fi
grep -Fq "$hub:V2 already exists" "$state/log"
[[ ! -s "$state/writes" ]]

# A warning mentioning a 404 cannot turn a failed lookup into permission to write
export denied_target="$hub:V3"
if publish "$ghcr:V3" "$hub:V3"; then
    echo "Expected a denied lookup to fail publication" >&2
    exit 1
fi
grep -Fq 'unauthorized [http 403]' "$state/log"
[[ ! -s "$state/writes" ]]
