#!/usr/bin/env bash

set -o errexit
set -o xtrace

bootstrapArgs=()
useClang='false'
keepArchive='false'
while getopts 'mck' OPT; do
	case "${OPT}" in
		m)
			bootstrapArgs+=('--with-libraries=thread,log,filesystem,program_options')
			;;
		c)
			useClang='true'
			;;
		k)
			keepArchive='true'
			;;
	esac
done

if ! c++ --version >/dev/null 2>/dev/null; then
	useClang='true'

	if ! clang++ --version >/dev/null 2>/dev/null; then
		echo "Unable to find a usable toolset" >&2

		exit 1
	fi
fi

if [ "${useClang}" = 'true' ]; then
	bootstrapArgs+=(--with-toolset=clang)
fi

BOOST_BASENAME=boost_1_66_0
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}
BOOST_URL=https://downloads.sourceforge.net/project/boost/boost/1.66.0/${BOOST_BASENAME}.tar.bz2
BOOST_ARCHIVE="${BOOST_BASENAME}.tar.bz2"
BOOST_ARCHIVE_SHA256='5721818253e6a0989583192f96782c4a98eb6204965316df9f5ad75819225ca9'

if [ ! -f "${BOOST_ARCHIVE}" ]; then
	wget --quiet -O "${BOOST_ARCHIVE}.new" "${BOOST_URL}"
	checkHash="$(openssl dgst -sha256 "${BOOST_ARCHIVE}.new" | sed 's@^.*= *@@')"
	if [ "${checkHash}" != "${BOOST_ARCHIVE_SHA256}" ]; then
		echo "Checksum mismatch.  Expected ${BOOST_ARCHIVE_SHA256}, got ${checkHash}" >&2

		exit 1
	fi
	mv "${BOOST_ARCHIVE}.new" "${BOOST_ARCHIVE}" || exit 1
else
	keepArchive='true'
fi

rm -rf "${BOOST_BASENAME}"
tar xf "${BOOST_ARCHIVE}"

pushd "${BOOST_BASENAME}"
./bootstrap.sh "${bootstrapArgs[@]}"
./b2 -d0 --prefix="${BOOST_ROOT}" link=static install
popd

rm -rf "${BOOST_BASENAME}"
if [ "${keepArchive}" != 'true' ]; then
	rm -f "${BOOST_ARCHIVE}"
fi
