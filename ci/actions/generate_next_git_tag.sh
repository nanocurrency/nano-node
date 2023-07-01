#!/bin/bash

# This script creates the next tag for the current branch by incrementing the version_pre_release by 1
# A new tag is only created if a new commit has been detected compared to the previous tag.
# The tag has the following format V${current_version_major}.${current_version_minor}${branch_name}
# ${branch_name} is converted to "DB" if the script operates on develop branch. (e.g first tag for V26: V26.0DB1)
# if -c flag is provided, version_pre_release in CMakeLists.txt is incremented and a new tag is created and pushed to origin
# if -o is provided, "build_tag" , "version_pre_release" and "tag_created" are written to file

set -e
output=""
create=false
tag_created="false"

while getopts ":o:c" opt; do
  case ${opt} in
    o )
      output=$OPTARG
      ;;
    c )
      create=true
      ;;
    \? )
      echo "Invalid Option: -$OPTARG" 1>&2
      exit 1
      ;;
    : )
      echo "Invalid Option: -$OPTARG requires an argument" 1>&2
      exit 1
      ;;
  esac
done
shift $((OPTIND -1))

# Fetch all existing tags
git fetch --tags -f

# Fetch the last commit hash of the current branch
current_commit_hash=$(git rev-parse HEAD)

# Fetch branch name
branch_name=$(git rev-parse --abbrev-ref HEAD)

# Check if the branch name is 'develop'
if [[ "$branch_name" == "develop" ]]; then
    branch_name="DB"
fi

# Replace special characters with underscores
branch_name=${branch_name//[^a-zA-Z0-9]/_}

# Fetch major and minor version numbers from CMakeLists.txt
current_version_major=$(grep "CPACK_PACKAGE_VERSION_MAJOR" CMakeLists.txt | grep -o "[0-9]\+")
current_version_minor=$(grep "CPACK_PACKAGE_VERSION_MINOR" CMakeLists.txt | grep -o "[0-9]\+")

# Construct the base version
base_version="V${current_version_major}.${current_version_minor}${branch_name}"

# Fetch the existing tags
existing_tags=$(git tag --list "${base_version}*" | grep -E "${base_version}[0-9]+$")

# If no tag exists, then just append 1 to the base version
if [[ -z "$existing_tags" ]]; then
    new_tag="${base_version}1"
    tag_created="true"
else
    # Fetch the last tag number, increment it, and append to the base version
    last_tag=$(echo "$existing_tags" | sort -V | tail -n1)
    last_tag_number=$(echo "$last_tag" | awk -F"${branch_name}" '{print $2}')

    # Fetch the commit hash of the last commit of the previous tag, ignoring the commit with the version change
    last_tag_commit_hash=$(git rev-list -n 2 $last_tag | tail -n 1)

    if [[ "$current_commit_hash" == "$last_tag_commit_hash" ]]; then
        echo "No new commits since the last tag. No new tag will be created."
    else
        next_tag_number=$((last_tag_number + 1))
        new_tag="${base_version}${next_tag_number}"
        tag_created="true"
    fi
fi
echo "$new_tag"

# Update output file
if [[ -n "$output" ]]; then
    echo "build_tag =$new_tag" > $output
    echo "version_pre_release =$next_tag_number" >> $output
    echo "tag_created =$tag_created" >> $output
fi

# Skip tag creation if no new commits
if [[ "$tag_created" == "false" ]]; then
    exit 0
fi

if [[ $create == true ]]; then
    # Stash current changes
    git config --global user.name "${GITHUB_ACTOR}"
    git config --global user.email "${GITHUB_ACTOR}@users.noreply.github.com"    

    # Update CPACK_PACKAGE_VERSION_PRE_RELEASE in CMakeLists.txt (macOs compatible sed -i.bak)
    sed -i.bak "s/set(CPACK_PACKAGE_VERSION_PRE_RELEASE \"[0-9]*\")/set(CPACK_PACKAGE_VERSION_PRE_RELEASE \"${next_tag_number}\")/g" CMakeLists.txt
    rm CMakeLists.txt.bak
    git add CMakeLists.txt
    git commit -m "Update CPACK_PACKAGE_VERSION_PRE_RELEASE to $next_tag_number"    

    # Create & Push the new tag
    git tag -a "$new_tag" -m "This tag was created with generate_next_git_tag.sh"
    git push origin "$new_tag"

    # Undo the last commit and apply stashed changes
    git reset --hard HEAD~1
    echo "The tag $new_tag has been created and pushed, but the commit used for the tag does not exist on any branch."
fi