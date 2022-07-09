#!/bin/bash

if [ -e "${2}" ]; then
	echo "makesrc <tag> <repo>" >&2
	echo "    tag      valid <tag> for <repo>" >&2
	echo "    repo     repository to build" >&2
	exit 1
fi

TAG="${1}"
repository="${2:-nanocurrency/nano-node}"
VERSION=$(echo "${TAG}" | sed 's/V//' | sed 's/-/_/g')
TAG_DATE=""
scriptDir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

function make_source() {
	git clone --recursive --single-branch --branch "${TAG}" "https://github.com/${repository}" "nano-${VERSION}"
	cd "nano-${VERSION}"
	git fetch --tags
	COUNT=$(git tag -l "${TAG}" | wc -l)
	if [ "$COUNT" -eq 0 ]; then
		echo "tag ${TAG} not found"
		exit 1
	else
		git checkout "${TAG}"
	fi
	# XXX:TODO: Update spec file with version information
	source_information
	rm -fr .git* .clang-format* .travis.yml appveyor.yml asan_blacklist ci docker util
	find . -type f ! -print 2>/dev/null | egrep -v '^\./(MD5SUMS|SHA256SUMS)$' | sort -u | sed s/'^\.\/'/''/ | sed 's/ /\\ /g' | xargs openssl md5 | sed 's@MD5(\(.*\))= \([0-9a-f]*\)@\2  \1@' >MD5SUMS 2>/dev/null
	find . -type f ! -print 2>/dev/null | egrep -v '^\./(SHA256SUMS)$' | sort -u | sed s/'^\.\/'/''/ | sed 's/ /\\ /g' | xargs openssl sha1 -sha256 | sed 's@SHA256(\(.*\))= \([0-9a-f]*\)@\2  \1@' >SHA256SUMS 2>/dev/null
	tarball_creation
}
function source_information() {
	DATE=$(git log --tags --simplify-by-decoration --pretty="format:%ai %d" | head -1 | cut -d " " -f1-3)
	COMMIT=$(git log | head -1 | cut -d " " -f 2)
	TAG_DATE=$(TZ=UTC date -d"${DATE}" +%s)
	export TAG_DATE
}
function cleanup_source() {
	mv "nano-${VERSION}.tar.gz" ~/.
	echo "ARCHIVE MOVDED TO HOME..."
	rm -fr "nano-${VERSION}"/
}

function tarball_creation() {
	cd ..
	ARCHIVE_FILE_NAME="nano-${VERSION}.tar.gz"
	echo "CREATING ${ARCHIVE_FILE_NAME}..."
	# Determine if we can create a stable archive
	tarArgs=()
	if tar -Pcf - /dev/null | tar --sort=name -Ptvf - >/dev/null 2>/dev/null; then
		tarArgs=("${tarArgs[@]}" --sort=name)
	fi
	if tar -Pcf - /dev/null | tar --owner=root:0 --group=root:0 -Ptvf - >/dev/null 2>/dev/null; then
		tarArgs=("${tarArgs[@]}" --owner=root:0 --group=root:0)
	fi
	if [ -n "${TAG_DATE}" ]; then
		if tar -Pcf - /dev/null | TZ=UTC tar --mtime="${TAG_DATE}" -Ptvf - >/dev/null 2>/dev/null; then
			tarArgs=("${tarArgs[@]}" --mtime="@${TAG_DATE}")
		fi
	fi
	TZ=UTC LANG=C LC_ALL=C tar "${tarArgs[@]}" -cvf - "nano-${VERSION}" | TZ=UTC gzip --no-name -9c >"${ARCHIVE_FILE_NAME}" || exit 1
}

set -x

make_source
cleanup_source
