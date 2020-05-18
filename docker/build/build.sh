#!/bin/bash

base='ubuntu'
tag_name='bananocoin/banano-build:latest'
docker_file='Dockerfile.build'

print_usage() {
	echo 'build.sh [-h] [-b {ubuntu|alpine}]'
}

while getopts 'hb:' OPT; do
	case "${OPT}" in
		h)
			print_usage
			exit 0
			;;
		b)
			base="${OPTARG}"
			;;
		*)
			print_usage >&2
			exit 1
			;;
	esac
done

case "${base}" in
	ubuntu)
		;;
	alpine)
        tag_name='bananocoin/banano-build:latest-alpine'
        docker_file='Dockerfile.build-alpine'
		;;
	*)
		echo "Invalid base: ${base}" >&2
		exit 1
		;;
esac

REPO_ROOT=`git rev-parse --show-toplevel`
pushd $REPO_ROOT
docker build -f docker/build/$docker_file -t $tag_name .
popd
