#!/bin/bash
set -euo pipefail

# Opens the release branch for the major the default branch is at and moves the default branch on.
#
# Run from the default branch, develop. It creates releases/v<major> from HEAD with the pre-release
# number set to 0 and pushes it, then opens a pull request that sets the default branch's version
# to <major+1>.0. From then on Tag Build makes release candidates and releases on the release branch
# and development builds of the next major on the default branch.
#
# Usage: ci/deploy/prepare-release.sh [--dry-run]

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

dry_run=false
if [[ "${1:-}" == "--dry-run" ]]; then
    dry_run=true
fi

branch=${GITHUB_REF_NAME:-$(git branch --show-current)}
default_branch=${DEFAULT_BRANCH:-develop}
start=$(git rev-parse HEAD)

major=$(cmake_version MAJOR)
minor=$(cmake_version MINOR)
if [[ -z "${major}" || -z "${minor}" ]]; then
    die "Could not read CPACK_PACKAGE_VERSION_MAJOR and MINOR from CMakeLists.txt"
fi
echo "Branch ${branch} at ${start:0:10}, CMakeLists.txt says version ${major}.${minor}"

if [[ "${branch}" != "${default_branch}" ]]; then
    die "Prepare Release runs on ${default_branch}, not on ${branch}"
fi
if [[ "${minor}" != 0 ]]; then
    die "${default_branch} should be at ${major}.0 when the release branch is cut, not ${major}.${minor}"
fi

release_branch="releases/v${major}"
next_major=$((major + 1))
bump_branch="version-${next_major}.0"
for existing in "${release_branch}" "${bump_branch}"; do
    if git ls-remote --exit-code --heads origin "${existing}" >/dev/null; then
        die "Branch ${existing} already exists on origin"
    fi
done

echo "Opening ${release_branch} at version ${major}.0 and moving ${default_branch} to ${next_major}.0 through ${bump_branch}"
if [[ "${dry_run}" == true ]]; then
    echo "Dry run, nothing written"
    exit 0
fi

git checkout --quiet -b "${release_branch}" "${start}"
set_cmake_version PRE_RELEASE 0
git add CMakeLists.txt
git_as_actor commit --quiet --message "Open ${release_branch} at version ${major}.0"
git push origin "refs/heads/${release_branch}"
echo "Pushed ${release_branch} at $(git rev-parse --short HEAD)"

git checkout --quiet -b "${bump_branch}" "${start}"
set_cmake_version MAJOR "${next_major}"
git add CMakeLists.txt
git_as_actor commit --quiet --message "Set ${default_branch} to version ${next_major}.0"
git push origin "refs/heads/${bump_branch}"
echo "Pushed ${bump_branch} at $(git rev-parse --short HEAD)"

pull_request=$(gh pr create ${GITHUB_REPOSITORY:+--repo "${GITHUB_REPOSITORY}"} --base "${default_branch}" --head "${bump_branch}" \
    --title "Set ${default_branch} to version ${next_major}.0" \
    --body "Opened by Prepare Release together with ${release_branch}. Merging it moves ${default_branch} to ${next_major}.0, so its builds are tagged V${next_major}.0DB<n> while ${release_branch} carries V${major}.0. Pull requests opened by a workflow get no checks of their own; push to the branch or close and reopen it if they are wanted.")
echo "Opened ${pull_request}"

echo "::notice::Opened ${release_branch}; merge ${pull_request} to move ${default_branch} to ${next_major}.0"
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
        echo "release_branch=${release_branch}"
        echo "pull_request=${pull_request}"
    } >>"${GITHUB_OUTPUT}"
fi
