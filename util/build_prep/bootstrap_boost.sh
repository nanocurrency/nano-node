#!/usr/bin/env bash

bootstrapArgs=()
buildArgs=()
useClang='false'
useLibCXX='false'
keepArchive='false'
LINK_TYPE=('link=static')
debugLevel=0
buildThreads=1
buildCArgs=()
buildCXXArgs=()
buildLDArgs=()
boostVersion='1.81.0'
while getopts 'hmscCkpvB:j:' OPT; do
	case "${OPT}" in
	h)
		echo "Usage: bootstrap_boost.sh [-hmcCkpv] [-B <boostVersion>]"
		echo "   -h                 This help"
		echo "   -s                 Build Shared and static libs, default is static only"
		echo "   -m                 Build a minimal set of libraries needed for Nano"
		echo "   -j <threads> 		Number of threads to build with"
		echo "   -c                 Use Clang"
		echo "   -C                 Use libc++ when using Clang"
		echo "   -k                 Keep the downloaded archive file"
		echo "   -p                 Build a PIC version of the objects"
		echo "   -v                 Increase debug level, may be repeated to increase it"
		echo "                      further"
		echo "   -B <boostVersion>  Specify version of Boost to build"
		exit 0
		;;
	s)
		LINK_TYPE+=('link=shared')
		;;
	m)
		bootstrapArgs+=('--with-libraries=system,thread,log,filesystem,program_options,coroutine,context')
		;;
	j)
		buildThreads=${OPTARG}
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
		debugLevel=$(($debugLevel + 1))
		;;
	B)
		boostVersion="${OPTARG}"
		;;
	esac
done

set -o errexit
set -o xtrace

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

case "${boostVersion}" in
1.81.0)
	BOOST_BASENAME=boost_1_81_0
	BOOST_URL=https://sourceforge.net/projects/boost/files/boost/1.81.0/${BOOST_BASENAME}.tar.bz2/download
	BOOST_ARCHIVE_SHA256='71feeed900fbccca04a3b4f2f84a7c217186f28a940ed8b7ed4725986baf99fa'
	;;
1.70)
	BOOST_BASENAME=boost_1_70_0
	BOOST_URL=https://sourceforge.net/projects/boost/files/boost/1.70.0/${BOOST_BASENAME}.tar.bz2/download
	BOOST_ARCHIVE_SHA256='430ae8354789de4fd19ee52f3b1f739e1fba576f0aded0897c3c2bc00fb38778'
	;;
1.72)
	BOOST_BASENAME=boost_1_72_0
	BOOST_URL=https://sourceforge.net/projects/boost/files/boost/1.72.0/${BOOST_BASENAME}.tar.bz2/download
	BOOST_ARCHIVE_SHA256='59c9b274bc451cf91a9ba1dd2c7fdcaf5d60b1b3aa83f2c9fa143417cc660722'
	;;
1.73)
	BOOST_BASENAME=boost_1_73_0
	BOOST_URL=https://sourceforge.net/projects/boost/files/boost/1.73.0/${BOOST_BASENAME}.tar.bz2/download
	BOOST_ARCHIVE_SHA256='4eb3b8d442b426dc35346235c8733b5ae35ba431690e38c6a8263dce9fcbb402'
	;;
1.75)
	BOOST_BASENAME=boost_1_75_0
	BOOST_URL=https://sourceforge.net/projects/boost/files/boost/1.75.0/${BOOST_BASENAME}.tar.bz2/download
	BOOST_ARCHIVE_SHA256='953db31e016db7bb207f11432bef7df100516eeb746843fa0486a222e3fd49cb'
	;;
*)
	echo "Unsupported Boost version: ${boostVersion}" >&2
	exit 1
	;;
esac
BOOST_ARCHIVE="${BOOST_BASENAME}.tar.bz2"
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}

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

if [ -n "${buildCArgs[*]}" ]; then
	buildArgs+=(cflags="${buildCArgs[*]}")
fi

if [ -n "${buildCXXArgs[*]}" ]; then
	buildArgs+=(cxxflags="${buildCXXArgs[*]}")
fi

if [ -n "${buildLDArgs[*]}" ]; then
	buildArgs+=(linkflags="${buildLDArgs[*]}")
fi

rm -rf "${BOOST_BASENAME}"
tar xf "${BOOST_ARCHIVE}"

pushd "${BOOST_BASENAME}"
./bootstrap.sh "${bootstrapArgs[@]}"
./b2 -d${debugLevel} -j${buildThreads} hardcode-dll-paths=true dll-path="'\$ORIGIN/../lib'" --prefix="${BOOST_ROOT}" ${LINK_TYPE[@]} "${buildArgs[@]}" install
popd

rm -rf "${BOOST_BASENAME}"
if [ "${keepArchive}" != 'true' ]; then
	rm -f "${BOOST_ARCHIVE}"
fi
