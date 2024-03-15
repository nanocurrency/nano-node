#!/bin/bash

# Script Description

# Purpose:
# This script generates a new Git tag based on the current branch and previously generated tags.
# It creates a new tag only if there's a new commit compared to the previous tag.

# Tag Format:
# General: V{MAJOR}.{MINOR}{tag_suffix}{increment}
# For releases/v branch: V{MAJOR}.{MINOR}

# Options:
# $IS_RELEASE_BUILD : Indicates a release build. In this case, {tag_suffix} is ignored.
#   New commit: Increments the {MINOR} version.
# -s {tag_suffix} : overwrites tag_suffix derived from the branch name. Derived tag_suffixes are :
#   DB for develop branch (e.g. V26.0DB1)
#   RC for releases/v branch (e.g. V26.0RC1)
#   {branch_name} for other branches (e.g. V26.0current_git_branch1)
# -c : Create and push the tag to origin.
# -o {output} : Write results to the specified output file.

set -e
set -x
output=""
push_tag=false
is_release_build=${IS_RELEASE_BUILD:-false}
tag_suffix=""

while getopts ":o:cs:" opt; do
    case ${opt} in
    o)
        output=$OPTARG
        ;;
    c)
        push_tag=true
        ;;
    s)
        tag_suffix=$OPTARG
        ;;
    \?)
        echo "Invalid Option: -$OPTARG" 1>&2
        exit 1
        ;;
    :)
        echo "Invalid Option: -$OPTARG requires an argument" 1>&2
        exit 1
        ;;
    esac
done
shift $((OPTIND - 1))

is_release_build() {
    [[ $is_release_branch == true && $is_release_build == true ]]
}

is_release_branch_and_release_tag_exists() {
    [[ $is_release_branch == true && $exists_tag_current_release == true ]]
}

get_tag_suffix() {
    local existing_suffix=$1
    local branch_name=$2

    # If tag_suffix is already provided, return it
    if [[ -n "$existing_suffix" ]]; then
        echo "$existing_suffix"
        return
    fi

    # Replace non-alphanumeric characters with underscores
    local new_tag_suffix=${branch_name//[^a-zA-Z0-9]/_}

    # Specific rules for certain branch names
    if [[ "$branch_name" == "develop" ]]; then
        new_tag_suffix="DB"
    elif [[ "$branch_name" =~ ^releases/v[0-9]+ ]]; then
        new_tag_suffix="RC"
    fi
    echo $new_tag_suffix
}

update_output() {
    #Responsible for either writing to file (-o flag) or to $GITHUB_ENV (when run from a workflow)
    local new_tag=$1
    local tag_created=$2

    if [[ -n "$output" ]]; then
        # Output to the specified file if -o is used
        echo "CI_TAG=${new_tag}" >"$output"
        echo "TAG_CREATED=${tag_created}" >>"$output"
    elif [[ $GITHUB_ACTIONS == 'true' ]]; then
        # Set environment variables if -o is not used
        echo "CI_TAG=${new_tag}" >>$GITHUB_ENV
        echo "TAG_CREATED=${tag_created}" >>$GITHUB_ENV
    else
        echo "Not running in a GitHub Actions environment. No action taken for CI_TAG, CI_TAG_NUMBER, TAG_CREATED."
    fi
}

update_cmake_lists() {
    local tag_types=("$@") # Array of tag types
    local variable_to_update=""

    for tag_type in "${tag_types[@]}"; do
        case "$tag_type" in
        "version_pre_release")
            variable_to_update="CPACK_PACKAGE_VERSION_PRE_RELEASE"
            new_tag_number=${tag_next_suffix_number}
            ;;
        "version_minor")
            variable_to_update="CPACK_PACKAGE_VERSION_MINOR"
            new_tag_number=${tag_next_minor_number}
            ;;
        esac

        if [[ -n "$variable_to_update" ]]; then
            echo "Update ${variable_to_update} to $new_tag_number"
            sed -i.bak "s/set(${variable_to_update} \"[0-9]*\")/set(${variable_to_update} \"${new_tag_number}\")/g" CMakeLists.txt
            rm CMakeLists.txt.bak
        fi
    done
    git add CMakeLists.txt
}

function create_commit() {
    git diff --cached --quiet
    local has_changes=$?              # store exit status of the last command
    if [[ $has_changes -eq 0 ]]; then # no changes
        echo "No changes to commit"
        echo "false"
    else # changes detected
        git commit -m "Update CMakeLists.txt" >/dev/null 2>&1
        echo "true"
    fi
}

# Fetch all existing tags
git fetch --tags -f

current_branch_name=$(git rev-parse --abbrev-ref HEAD)
current_commit_hash=$(git rev-parse HEAD)
current_version_major=$(grep "CPACK_PACKAGE_VERSION_MAJOR" CMakeLists.txt | grep -o "[0-9]\+")
current_version_minor=$(grep "CPACK_PACKAGE_VERSION_MINOR" CMakeLists.txt | grep -o "[0-9]\+")
declare -a cmake_versions_to_update

is_release_branch=$(echo "$current_branch_name" | grep -q "releases/v$current_version_major" && echo true || echo false)
tag_current_release="V${current_version_major}.${current_version_minor}"
exists_tag_current_release=$(git tag --list "${tag_current_release}" | grep -qE "${tag_current_release}$" && echo true || echo false)

# Determine the tag type and base version format
if is_release_build; then
    cmake_versions_to_update+=("version_minor")
    tag_base="${tag_current_release}"
else
    if is_release_branch_and_release_tag_exists; then
        # Make sure RC builds have release_build_minor_version + 1
        current_version_minor=$((current_version_minor + 1))
        cmake_versions_to_update+=("version_minor")
    fi
    cmake_versions_to_update+=("version_pre_release")
    tag_suffix=$(get_tag_suffix "$tag_suffix" "$current_branch_name")
    tag_base="V${current_version_major}.${current_version_minor}${tag_suffix}"
fi
tag_next_suffix_number=1                       # Will be overwritten if a previous tag exists
tag_next_minor_number=${current_version_minor} # Default value if no previous tag exists

# Fetch existing tags based on the base version
existing_tags=$(git tag --list "${tag_base}*" | grep -E "${tag_base}[0-9]*$" || true)
should_create_tag="true"

# Get next tag if a previous tag exists:
if [[ -n "$existing_tags" ]]; then
    most_recent_tag=$(echo "$existing_tags" | sort -V | tail -n1)

    if is_release_build; then
        # Increment the minor version for release builds (-r flag is set) or RC builds if the release tag exists
        tag_next_minor_number=$((current_version_minor + 1))
    else
        tag_next_minor_number=${current_version_minor}
    fi

    # Increment the suffix number based on the existing tags
    if [[ -n "$tag_suffix" && -n "$most_recent_tag" ]]; then
        tag_max_suffix_number=$(echo "$most_recent_tag" | awk -F"${tag_suffix}" '{print $2}')
        tag_next_suffix_number=$((tag_max_suffix_number + 1))
    fi
# Else if no previous tag matching tag_base exists, use default values set above
fi

# Check if the current commit is included in the last tag
tags_containing_current_commit=$(git tag --contains "$current_commit_hash")
if [[ -n "$most_recent_tag" ]] && echo "$tags_containing_current_commit" | grep -q "$most_recent_tag"; then
    should_create_tag="false"
fi

# Generate the new tag name
if is_release_build; then
    # tag_suffix is ignored for release builds
    new_tag="V${current_version_major}.${tag_next_minor_number}"
else
    new_tag="${tag_base}${tag_next_suffix_number}"
fi

update_output $new_tag $should_create_tag

# Skip tag creation if no new commits
if [[ "$should_create_tag" == "true" ]]; then
    echo "Tag '$new_tag' ready to be created"
else
    echo "No new commits. Tag '$new_tag' will not be created."
    exit 0
fi

if [[ $push_tag == true ]]; then
    # Stash current changes
    git config user.name "${GITHUB_ACTOR}"
    git config user.email "${GITHUB_ACTOR}@users.noreply.github.com"

    # Update variable in CMakeLists.txt
    update_cmake_lists "${cmake_versions_to_update[@]}"

    commit_made=$(create_commit)

    git tag -fa "$new_tag" -m "This tag was created with generate_next_git_tag.sh"
    git push origin "$new_tag" -f
    echo "The tag $new_tag has been created and pushed."

    # If it's a release build, also push the commit to the branch
    if is_release_build; then
        git push origin "$current_branch_name" -f
        echo "The commit has been pushed to the $current_branch_name branch."

    elif [[ "$commit_made" == "true" ]]; then
        # Resets the last commit on non-release branches after tagging, keeping the current branch clean.
        git reset --hard HEAD~1
        echo "The commit used for the tag does not exist on any branch."
    fi

else
    echo "Tag was not created. Run the script with -c option to create and push the tag"
fi
