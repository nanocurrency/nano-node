#!/bin/bash

# This script is just a wrapper for the changelog.py script, it has the functionality of automatically discovering the
# last version based on the tags saved in the repository.
# TODO: move the implemented functionalities of this script into the principal one: 'changelog.py'

set -e

TAG=$(echo "${TAG}")
PAT=$(echo "${PAT}")

print_usage() {
    echo "$(basename ${0}) [OPTIONS]"
    echo "OPTIONS:"
    echo "  [-h]               Print this help info."
    echo "  [-p <pat>]         Personal Access Token. Necessary if the PAT variable is not set."
    echo "  [-t <version_tag>  Version tag. Necessary if the TAG variable is not set. The -t requires the"
    echo "                     formats: V1.0 that can be appended by RC1 or DB1. If -b is not provided, it'll"
    echo "                     look for the last version before it on the tag list."
    echo "  [-b <commit>]      Specifies the start commit for the changelog."
    echo "  [-s <source_dir>]  Directory where the source-code will be downloaded to. Default is \$PWD."
    echo "  [-o <output_dir>]  Directory where the changelog will be generated. Default is \$PWD."
    echo "  [-w <workspace>]   Directory where the changelog.py can be found. Default is \$PWD."
    echo "  [-r <repository>]  Repository name to be passed to changelog.py. Default is 'nanocurrency/nano-node'."
    echo "  [-i]               Install Python dependencies."
}

revision_begin=""
source_dir="$(pwd)"
output_dir="$(pwd)"
workspace="$(pwd)"
repository="nanocurrency/nano-node"
install_deps=false

while getopts 'ht:t:p:b:s:o:w:r:i' OPT; do
    case "${OPT}" in
    h)
        print_usage
        exit 0
        ;;
    t)
        if [[ -n "$TAG" ]]; then
            echo "Ignoring the TAG environment variable"
        fi
        TAG="${OPTARG}"
        ;;
    p)
        if [[ -n "$PAT" ]]; then
            echo "Ignoring the PAT environment variable"
        fi
        PAT="${OPTARG}"
        ;;
    b)
        revision_begin="${OPTARG}"
        if [[ -z "$revision_begin" ]]; then
            echo "Invalid revision"
            exit 1
        fi
        ;;
    s)
        source_dir="${OPTARG}"
        if [[ ! -d "$source_dir" ]]; then
            echo "Invalid source directory"
            exit 1
        fi
        ;;
    o)
        output_dir="${OPTARG}"
        if [[ ! -d "$output_dir" ]]; then
            echo "Invalid output directory"
            exit 1
        fi
        ;;
    w)
        workspace="${OPTARG}"
        if [[ ! -d "$workspace" ]]; then
            echo "Invalid workspace directory"
            exit 1
        fi
        ;;
    r)
        repository="${OPTARG}"
        if [[ -z "$repository" ]]; then
            echo "Invalid repository"
            exit 1
        fi
        ;;
    i)
        install_deps=true
        ;;
    *)
        print_usage >&2
        exit 1
        ;;
    esac
done

# matches V1.0.0 and V1.0 formats
version_re="^(V[0-9]+.[0-9]+(.[0-9]+)?)$"
# matches V1.0.0RC1, V1.0.0DB1, V1.0RC1, V1.0DB1 formats
rc_beta_re="^(V[0-9]+.[0-9]+(.[0-9]+)?((RC[0-9]+)|(DB[0-9]+))?)$"

echo "Validating the required input variables"
(
    set -x
    if [[ -z "${TAG}" ]]; then
        echo "The TAG must be set by either the environment variable TAG or by the -t option"
        exit 1
    fi

    set +x
    if [[ -z "${PAT}" ]]; then
        echo "The PAT environment variable must be set"
        exit 1
    fi
    set -x

    if [[ -n "${TAG}" ]]; then
        if [[ "${TAG}" =~ $version_re ]]; then
            exit 0
        elif [[ "${TAG}" =~ $rc_beta_re ]]; then
            echo "RC and DB tags are not supported"
            exit 1
        else
            echo "The tag must match the pattern V1.0 or V1.0.0"
            exit 1
        fi
    fi
) || exit 1

if [[ -n "$revision_begin" ]]; then
    echo "Selected the interval from $revision_begin to $TAG"
fi

set -x

echo "Checking out the specified tag"
pushd "$source_dir"
if [[ ! -z $(ls -A "$source_dir" ) ]]; then
    popd
    echo "The source directory: ${source_dir} is not empty"
    exit 1
fi

git clone --branch "${TAG}" "https://github.com/${repository}" "nano-${TAG}"
pushd "nano-${TAG}"
git fetch --tags

read -r version_major version_minor version_revision <<< $( echo "${TAG}" | awk -F 'V' {'print $2'} | awk -F \. {'print $1, $2, $3'} )
if [[ -n "$revision_begin" ]]; then
    newest_previous_version="$revision_begin"
else
    if [[ -n "${version_revision}" ]]; then
        echo "Version revision is currently not supported for automatic interval"
        exit 1
    fi

    echo "Getting the tag of the most recent previous version"
    newest_previous_version=""
    previous_version_major="$version_major"
    previous_version_minor="$version_minor"
    while [[ -z "$newest_previous_version" ]]; do
        if [[ $previous_version_minor == "0" ]]; then
            previous_version_major=$(( previous_version_major-1 ))
            previous_version_minor="[0-9]+"
        else
            previous_version_major=$version_major
            previous_version_minor=$(( previous_version_minor-1 ))
        fi
        version_tags=$(git tag | sort -r | grep -E "^(V($previous_version_major).($previous_version_minor)(.[0-9]+)?)$")
        for tag in $version_tags; do
            if [[ -n "$tag" ]]; then
                newest_previous_version=$tag
                echo "Found tag: $tag"
                break
            fi
        done
    done
    if [[ -z "$newest_previous_version" ]]; then
        echo "Didn't find a tag for the previous version"
        exit 1
    fi
fi

echo "Setting the python environment and running the changelog.py script"
if [[ $install_deps == true ]]; then
    echo "Installing Python dependencies"
    apt-get install -yqq python3.8 python3-pip virtualenv python3-venv
fi
(
    set -e

    virtualenv "${workspace}/venv" --python=python3.8
    source "${workspace}/venv/bin/activate"
    python -m pip install PyGithub mdutils
    set +x
    if [[ -n "${revision_begin}" ]]; then
        echo "Tracking the changes from ${revision_begin} to ${TAG}"
        python "${workspace}/util/changelog.py" --pat "${PAT}" -s "${revision_begin}" -e "${TAG}" -r "${repository}"
    else
        read -r newest_version_major <<< $( echo "${TAG}" | awk -F 'V' {'print $2'} | awk -F \. {'print $1'} )
        if [[ ${version_major} -gt ${newest_version_major} ]]; then
            # Finding the common ancestor is necessary in case the newest previous change is on a different branch than
            # the TAG, so it will track from the newest previous change ancestor on the development branch up to the TAG
            develop_head=$(git show-ref -s origin/develop)
            common_ancestor=$(git merge-base --octopus "${develop_head}" "${newest_previous_version}")
            echo "Tracking the changes from a common ancestor between the develop branch and ${newest_previous_version}"
            python "${workspace}/util/changelog.py" --pat "${PAT}" -s "${common_ancestor}" -e "${TAG}" -r "${repository}"
        else
            echo "Tracking the changes from ${newest_previous_version} to ${TAG}"
            python "${workspace}/util/changelog.py" --pat "${PAT}" -s "${newest_previous_version}" -e "${TAG}" -r "${repository}"
        fi
    fi
    set -x

    if [ ! -s CHANGELOG.md ]; then
        echo "CHANGELOG not generated"
        exit 1
    else
        mv -vn CHANGELOG.md -t "${output_dir}"
    fi
    exit 0
) || exit 1
