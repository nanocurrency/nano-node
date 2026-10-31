#!/bin/bash
set -euo pipefail

# Creates the next build tag for the checked-out branch and hands it to the workflow.
#
# Build tags are V<major>.<minor>DB<n> on develop, V<major>.<minor>RC<n> on the release branch
# of the current major and V<major>.<minor>-<branch>-<n> elsewhere. With RELEASE=true, accepted
# only on a release branch, the tag is a plain V<major>.<minor>. The version numbers come from
# CMakeLists.txt, which is updated to match and committed first, so the tag carries the version
# it names. The commit of a release is pushed to the branch; a pre-release commit exists only
# through its tag. Nothing is created while the latest tag of the kind already covers HEAD.
#
# Usage: ci/deploy/next-tag.sh [--dry-run]

dry_run=false
if [[ "${1:-}" == "--dry-run" ]]; then
    dry_run=true
fi

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

release=${RELEASE:-false}
branch=${GITHUB_REF_NAME:-$(git branch --show-current)}

tag_exists() {
    git show-ref --verify --quiet "refs/tags/$1"
}

# The highest <n> among the tags <base><n>, empty when there is none
latest_number() {
    local pattern
    pattern="^$(sed 's/\./\\./g' <<<"$1")[0-9]+$"
    git tag --list "$1*" | { grep -E "${pattern}" || true; } | sed 's/^.*[^0-9]//' | sort -n | tail -n 1
}

# A tag covers HEAD when it points at HEAD or at a version commit made on top of it
covers_head() {
    git merge-base --is-ancestor HEAD "$1"
}

major=$(cmake_version MAJOR)
minor=$(cmake_version MINOR)
if [[ -z "${major}" || -z "${minor}" ]]; then
    die "Could not read CPACK_PACKAGE_VERSION_MAJOR and MINOR from CMakeLists.txt"
fi
echo "Branch ${branch} at $(git rev-parse --short HEAD), CMakeLists.txt says version ${major}.${minor}"

release_branch=false
case "${branch}" in
    develop)
        suffix=DB
        echo "develop builds are tagged V${major}.${minor}DB<n>"
        ;;
    "releases/v${major}")
        suffix=RC
        release_branch=true
        echo "Release branch of ${major}: candidates are tagged V${major}.<minor>RC<n>, releases V${major}.<minor>"
        ;;
    *)
        suffix="-${branch//[^a-zA-Z0-9]/_}-"
        echo "Builds of this branch are tagged V${major}.${minor}${suffix}<n>"
        ;;
esac

if [[ "${release}" == true && "${release_branch}" != true ]]; then
    die "Release tags are created from releases/v${major}, not from ${branch}"
fi

# Once a minor is released, the release branch moves on to the next one
last_release=""
if [[ "${release_branch}" == true ]]; then
    while tag_exists "V${major}.${minor}"; do
        last_release="V${major}.${minor}"
        minor=$((minor + 1))
    done
    if [[ -n "${last_release}" ]]; then
        echo "${last_release} is released, so this branch is now at ${major}.${minor}"
    fi
fi

# The latest tag of the same kind decides whether there is anything new to tag
previous=""
if [[ "${release}" == true ]]; then
    tag="V${major}.${minor}"
    previous=${last_release}
else
    base="V${major}.${minor}${suffix}"
    number=$(latest_number "${base}")
    if [[ -n "${number}" ]]; then
        previous="${base}${number}"
    fi
    number=$((${number:-0} + 1))
    tag="${base}${number}"
fi
echo "Latest tag of this kind: ${previous:-none}"

if [[ -n "${previous}" ]] && covers_head "${previous}"; then
    echo "HEAD is already covered by ${previous}, nothing new to tag"
    if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
        echo "created=false" >>"${GITHUB_OUTPUT}"
    fi
    exit 0
fi

if tag_exists "${tag}"; then
    die "Tag ${tag} already exists"
fi

echo "Next tag: ${tag}"
if [[ "${dry_run}" == true ]]; then
    echo "Dry run, nothing written"
    exit 0
fi

set_cmake_version MINOR "${minor}"
if [[ "${release}" == true ]]; then
    echo "Writing version ${major}.${minor} into CMakeLists.txt"
else
    set_cmake_version PRE_RELEASE "${number}"
    echo "Writing version ${major}.${minor} pre-release ${number} into CMakeLists.txt"
fi
git add CMakeLists.txt
if git diff --cached --quiet; then
    echo "CMakeLists.txt already had that version, tagging HEAD as it is"
else
    git_as_actor commit --quiet --message "Set version for ${tag}"
    echo "Committed the version as $(git rev-parse --short HEAD)"
fi

git_as_actor tag --annotate --message "Build tag ${tag}" "${tag}"
git push origin "refs/tags/${tag}"
echo "Pushed tag ${tag}"
if [[ "${release}" == true ]]; then
    git push origin "HEAD:refs/heads/${branch}"
    echo "Pushed the version commit to ${branch}"
fi

echo "::notice::Created ${tag} from ${branch}"
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
        echo "tag=${tag}"
        echo "created=true"
    } >>"${GITHUB_OUTPUT}"
fi
