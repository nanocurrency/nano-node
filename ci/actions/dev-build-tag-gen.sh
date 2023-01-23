#!/bin/bash

# This script gets the last DB tag for the current release version and checks whether the develop branch contains new
# commits since the last develop build. If so, it sets and outputs the variable $build_tag with the correct numbering
# for the next DB build, and the build number used for it as $build_number variable.
# If the option -r is set, then it looks for the latest release branch, numbered as '<current major version>-1'.
# The -r option also outputs the release branch name as $release_branch.
# Error exit codes:
# 0: success, the build tag was generated!
# 1: branch error or invalid usage of the script.
# 2: no new change found since the last build.

source_dir="$(pwd)"
git_upstream="origin"
previous_release_gen=false
output_file=""

function print_short_usage {
    echo "$(basename ${0}) -o <output_file> [OPTIONS]"
    echo "Specify -h to see the options."
}

function print_usage {
    echo "$(basename ${0}) -o <output_file> [OPTIONS]"
    echo "ARGUMENTS:"
    echo "  -o <output_file>     Export the variables to an output file (sourcing this script is deprecated)."
    echo
    echo "OPTIONS:"
    echo "  [-h]                 Print this help info."
    echo "  [-s <source_dir>]    Directory that contains the source-code. Default is \$PWD."
    echo "  [-u <git_upstream>]  Name of the git repository upstream. Default is \"${git_upstream}\"."
    echo "  [-r]                 Generates build tag for the latest release branch."
}

while getopts 'hs:u:ro:' OPT; do
    case "${OPT}" in
    h)
        print_usage
        exit 0
        ;;
    s)
        source_dir="${OPTARG}"
        if [[ ! -d "$source_dir" ]]; then
            echo "error: invalid source directory"
            exit 1
        fi
        ;;
    u)
        git_upstream="${OPTARG}"
        if [[ -z "$git_upstream" ]]; then
            echo "error: invalid git upstream"
            exit 1
        fi
        ;;
    r)
        previous_release_gen=true
        ;;
    o)
        output_file="${OPTARG}"
        if [[ -f "$output_file" ]]; then
            echo "error: the provided output_file already exists"
            exit 1
        fi
        ;;
    *)
        print_usage >&2
        exit 1
        ;;
    esac
done

if [[ -z "$output_file" ]]; then
    echo "error: invalid file name for exporting the variables"
    print_short_usage >&2
    exit 1
fi

function get_first_item {
    local list="$1"
    for item in $list; do
        if [[ -n "$item" ]]; then
            echo "$item"
            break
        fi
    done
}

function output_variable {
    if [[ $# -ne 1 ]]; then
        echo "illegal number of parameters"
        exit 1
    fi
    local var_name="$1"
    local var_value=${!var_name}
    if [[ -n "$output_file" ]]; then
        echo "${var_name}=${var_value}" >> "$output_file"
    fi
}

set -o nounset
set -o xtrace

current_version_major=$(grep -P "(set)(.)*(CPACK_PACKAGE_VERSION_MAJOR)" "${source_dir}/CMakeLists.txt" | grep -oP "([0-9]+)")
current_version_minor=$(grep -P "(set)(.)*(CPACK_PACKAGE_VERSION_MINOR)" "${source_dir}/CMakeLists.txt" | grep -oP "([0-9]+)")
current_version_pre_release=$(grep -P "(set)(.)*(CPACK_PACKAGE_VERSION_PRE_RELEASE)" "${source_dir}/CMakeLists.txt" | grep -oP "([0-9]+)")

version_tags=$(git tag | sort -V -r | grep -E "^(V([0-9]+).([0-9]+)(RC[0-9]+)?)$")
last_tag=$(get_first_item "$version_tags")
tag_version_major=$(echo "$last_tag" | grep -oP "\V([0-9]+)\." | grep -oP "[0-9]+")
if [[ ${tag_version_major} -ge ${current_version_major} ]]; then
    echo "error: this is not the develop branch or your higher tag version is not equivalent to the current major version."
    exit 1
fi

if [[ ${current_version_minor} != "0" ]]; then
    echo "error: this is not the develop branch or the version-minor number is not properly set."
    exit 1
fi

if [[ ${current_version_pre_release} != "99" ]]; then
    echo "error this is not the develop branch or the pre-release version is not properly set."
    exit 1
fi

pushd "$source_dir"

last_tag=""
version_tags=""
previous_release_major=0
previous_release_minor=0
if [[ $previous_release_gen == false ]]; then
    version_tags=$(git tag | sort -V -r | grep -E "^(V(${current_version_major}).(${current_version_minor})(DB[0-9]+))$" || true)
    last_tag=$(get_first_item "$version_tags")
else
    previous_release_major=$(( current_version_major - 1 ))
    version_tags=$(git tag | sort -V -r | grep -E "^(V(${previous_release_major}).([0-9]+))$" || true)
    if [[ -z "$version_tags" ]]; then
        previous_release_minor=0
    else
        last_minor_release=$(get_first_item "$version_tags")
        last_minor=$(echo "$last_minor_release" | grep -oP "\.([0-9]+)" | grep -oP "[0-9]+")
        previous_release_minor=$(( last_minor + 1 ))
    fi
    version_tags=$(git tag | sort -V -r | grep -E "^(V(${previous_release_major}).(${previous_release_minor})(DB[0-9]+)?)$" || true)
    last_tag=$(get_first_item "$version_tags")
    release_branch="releases/v${previous_release_major}"
    output_variable release_branch
fi
popd

build_tag=""
if [[ -z "$last_tag" ]]; then
    build_number=1
    echo "info: no tag found, build_number=${build_number}"
    if [[ $previous_release_gen == false ]]; then
        build_tag="V${current_version_major}.${current_version_minor}DB${build_number}"
    else
        build_tag="V${previous_release_major}.${previous_release_minor}DB${build_number}"
    fi
    output_variable build_number
    output_variable build_tag
    exit 0
fi

pushd "$source_dir"
develop_head=""
if [[ $previous_release_gen == false ]]; then
    develop_head=$(git rev-parse "${git_upstream}/develop")
else
    develop_head=$(git rev-parse "${git_upstream}/${release_branch}")
fi
tag_head=$(git rev-list "$last_tag" | head -n 1)
popd

if [[ "$develop_head" == "$tag_head" ]]; then
    echo "error: no new commits for the develop build, the develop (or release) branch head matches the latest DB tag head!"
    exit 2
fi

latest_build_number=$(echo "$last_tag" | grep -oP "(DB[0-9]+)" | grep -oP "[0-9]+")
build_number=$(( latest_build_number + 1 ))
if [[ $previous_release_gen == false ]]; then
    build_tag="V${current_version_major}.${current_version_minor}DB${build_number}"
else
    build_tag="V${previous_release_major}.${previous_release_minor}DB${build_number}"
fi
output_variable build_number
output_variable build_tag

set +o nounset
set +o xtrace
