#!/usr/bin/env bash

set -o nounset
set -o xtrace

run_load_test() {
	pushd load-tester
	cargo build --release
	popd
	cp ./load-tester/target/release/raiblocks-load-tester ./build/load_test
	timeout 420 ./load_test ./rai_node
	load_test_res=${?}
}

./ci/check-clang-format.sh
clang_format_res=$?

run_load_test
load_test_res=$?

doxygen doxygen.config
doxygen_res=$?

exit $((${load_test_res} + ${clang_format_res} + ${doxygen_res}))
