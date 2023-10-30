#!/bin/bash
set -euo pipefail

CLANG_VERSION=16

# TODO: Verify integrity (at this time, the clang build is not used for any production artifacts)
curl -O https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && ./llvm.sh $CLANG_VERSION

update-alternatives --install /usr/bin/cc cc /usr/bin/clang-$CLANG_VERSION 100
update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-$CLANG_VERSION 100
update-alternatives --install /usr/bin/lldb lldb /usr/bin/lldb-$CLANG_VERSION 100

# Workaround to get a path that can be easily passed into cmake for BOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE
# See https://www.boost.org/doc/libs/1_70_0/doc/html/stacktrace/configuration_and_build.html#stacktrace.configuration_and_build.f3
backtrace_file=$(find /usr/lib/gcc/ -name 'backtrace.h' | head -n 1) && test -f $backtrace_file && ln -s $backtrace_file /tmp/backtrace.h