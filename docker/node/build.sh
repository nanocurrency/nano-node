#!/bin/bash

network='live'
base='ubuntu'
alpine_tag=''
docker_file='Dockerfile'

print_usage() {
	echo 'build.sh [-h] [-n {live|beta|test}] [-b {ubuntu|alpine}]'
}

while getopts 'hn:b:' OPT; do
	case "${OPT}" in
		h)
			print_usage
			exit 0
			;;
		n)
			network="${OPTARG}"
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

case "${network}" in
	live)
		network_tag=''
		;;
	test|beta)
		network_tag="-${network}"
		;;
	*)
		echo "Invalid network: ${network}" >&2
		exit 1
		;;
esac

case "${base}" in
	ubuntu)
		;;
	alpine)
		alpine_tag="-alpine"
		docker_file="Dockerfile-alpine"	
		;;
	*)
		echo "Invalid base: ${base}" >&2
		exit 1
		;;
esac

REPO_ROOT=`git rev-parse --show-toplevel`
COMMIT_SHA=`git rev-parse --short HEAD`
pushd $REPO_ROOT
docker build --build-arg NETWORK="${network}" -f docker/node/${docker_file} -t bananocoin/banano${network_tag}:latest${alpine_tag} .
popd