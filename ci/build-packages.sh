#!/bin/bash
set -euo pipefail

# Builds the release packages for one network and collects them in packages/.
# NETWORK picks beta or live, CI_TAG is the build tag the packages are named after.

die() {
    echo "::error::$*" >&2
    exit 1
}

case "${NETWORK:-}" in
    beta)
        network=beta
        build_type=RelWithDebInfo
        ;;
    live)
        network=live
        build_type=Release
        ;;
    *)
        die "NETWORK must be beta or live, got '${NETWORK:-}'"
        ;;
esac

tag=${CI_TAG:-}
if [[ -z "${tag}" ]]; then
    die "CI_TAG must name the build tag"
fi
script_dir=$(dirname "${BASH_SOURCE[0]}")

echo "Building the ${network} network ${build_type} packages for ${tag}"

# The Windows installer bundles the Visual C++ runtime, which the install rules expect in the build directory
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
    echo "Fetching the Visual C++ redistributable for the installer"
    mkdir -p build
    curl -fsSL -o build/vc_redist.x64.exe https://aka.ms/vs/17/release/vc_redist.x64.exe
fi

NANO_NETWORK=${network} \
    BUILD_TYPE=${build_type} \
    NANO_GUI=ON \
    CI_TAG=${tag} \
    "${script_dir}/build.sh"

# Run the cpack that ships with cmake: the Windows runners also have Chocolatey's cpack on the PATH
cpack="$(dirname "$(command -v cmake)")/cpack"
echo "Packaging with ${cpack}"
(cd build && "${cpack}" -C "${build_type}")

mkdir -p packages
mv build/nano-node-"${tag}"-* packages/

echo "Packages built:"
ls -l packages
