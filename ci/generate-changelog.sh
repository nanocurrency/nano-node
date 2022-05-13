#!/bin/bash

set -e

TAG=$(echo "${TAG}")
PAT=$(echo "${PAT}")
source_dir="${1:-$(pwd)}"
output_dir="${2:-$(pwd)}"

pushd "$source_dir"
if [[ ! -d .git ]]; then
    echo "The source directory: ${source_dir} is not a git repository"
    exit 1
fi

# matches V1.0.0 and V1.0 formats
version_re="^(V[0-9]+.[0-9]+(.[0-9]+)?)$"
# matches V1.0.0RC1, V1.0.0DB1, V1.0RC1, V1.0DB1 formats
rc_beta_re="^(V[0-9]+.[0-9]+(.[0-9]+)?((RC[0-9]+)|(DB[0-9]+))?)$"

echo "Validating the required input variables TAG and PAT"
(
    if [[ -z "$TAG" || -z "$PAT" ]]; then
        echo "TAG and PAT environment variables must be set"
        exit 1
    fi

    if [[ ${TAG} =~ $version_re ]]; then
        exit 0
    elif [[ ${TAG} =~ $rc_beta_re ]]; then
        echo "RC and DB tags are not supported"
        exit 1
    else
        echo "The tag must match the pattern V1.0 or V1.0.0"
        exit 1
    fi
) || exit 1

read -r version_major version_minor version_revision <<< $( echo "${TAG}" | awk -F 'V' {'print $2'} | awk -F \. {'print $1, $2, $3'} )
previous_version_major=$(( version_major - 1 ))

echo "Getting the tag of the most recent previous version"
version_tags=$(git tag | grep -E "^(V(${previous_version_major}).[0-9]+(.[0-9]+)?)$" | sort)
for tag in $version_tags; do
    newest_previous_version=$tag
done
if [[ -z "$newest_previous_version" ]]; then
    echo "Didn't find a tag for the previous version: V${previous_version_major}.0 or V${previous_version_major}.0.0"
    exit 1
fi

develop_head=$(git show-ref -s origin/develop)
common_ancestor=$(git merge-base --octopus "${develop_head}" "${newest_previous_version}")

echo "Setting the python environment and running the changelog.py script"
apt-get install -y python3.8 python3-pip python3-venv
(
    virtualenv venv --python=python3.8
    source venv/bin/activate
    python -m pip install PyGithub mdutils
    python util/changelog.py -p "${PAT}" -s "${common_ancestor}" -e "${TAG}"

    if [ ! -s CHANGELOG.md ]; then
        echo "CHANGELOG not generated"
        exit 1
    else
        mv -vn CHANGELOG.md -t "${output_dir}"
    fi
    exit 0
) || exit 1
