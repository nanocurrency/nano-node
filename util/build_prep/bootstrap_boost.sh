#!/usr/bin/env bash

set -o errexit
set -o xtrace

bootstrapArgs=()
buildArgs=()
useClang='false'
useLibCXX='false'
keepArchive='false'
debugLevel=0
buildCArgs=()
buildCXXArgs=()
buildLDArgs=()
while getopts 'mcCkpv' OPT; do
	case "${OPT}" in
		m)
			bootstrapArgs+=('--with-libraries=thread,log,filesystem,program_options')
			;;
		c)
			useClang='true'
			;;
		C)
			useLibCXX='true'
			;;
		k)
			keepArchive='true'
			;;
		p)
			buildCXXArgs+=(-fPIC)
			buildCArgs+=(-fPIC)
			;;
		v)
			debugLevel=$[$debugLevel + 1]
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
	buildArgs+=(toolset=clang)
	if [ "${useLibCXX}" = 'true' ]; then
		buildCXXArgs+=(-stdlib=libc++)
		buildLDArgs+=(-stdlib=libc++)
	fi
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

if [ -n  "${buildCArgs[*]}" ]; then
	buildArgs+=(cflags="${buildCArgs[*]}")
fi

if [ -n  "${buildCXXArgs[*]}" ]; then
	buildArgs+=(cxxflags="${buildCXXArgs[*]}")
fi

if [ -n  "${buildLDArgs[*]}" ]; then
	buildArgs+=(linkflags="${buildLDArgs[*]}")
fi

rm -rf "${BOOST_BASENAME}"
tar xf "${BOOST_ARCHIVE}"

pushd "${BOOST_BASENAME}"
./bootstrap.sh "${bootstrapArgs[@]}"
./b2 -d${debugLevel} --prefix="${BOOST_ROOT}" link=static "${buildArgs[@]}" install
popd

rm -rf "${BOOST_BASENAME}"
if [ "${keepArchive}" != 'true' ]; then
	rm -f "${BOOST_ARCHIVE}"
fi
