#!/bin/bash
# Helpers shared by the deploy scripts that edit the version in CMakeLists.txt. Source it, do not run it.

die() {
    echo "::error::$*" >&2
    exit 1
}

# One of the version numbers CMakeLists.txt sets: MAJOR, MINOR or PRE_RELEASE
cmake_version() {
    sed -n "s/^[[:space:]]*set(CPACK_PACKAGE_VERSION_$1 \"\([0-9]*\)\")/\1/p" CMakeLists.txt
}

set_cmake_version() {
    sed -i.bak "s/^\([[:space:]]*\)set(CPACK_PACKAGE_VERSION_$1 \"[0-9]*\")/\1set(CPACK_PACKAGE_VERSION_$1 \"$2\")/" CMakeLists.txt
    rm CMakeLists.txt.bak
}

# git acting as the workflow's actor, or as the local user outside Actions
git_as_actor() {
    local actor=${GITHUB_ACTOR:-$(whoami)}
    git -c user.name="${actor}" -c user.email="${actor}@users.noreply.github.com" "$@"
}
