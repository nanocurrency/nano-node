#!/bin/bash
set -euo pipefail

apt-get update -qq && apt-get install -yqq \
clang \
lldb

export CXX=/usr/bin/clang++
export CC=/usr/bin/clang
update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100
update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100

# workaround to get a path that can be easily passed into cmake for
# BOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE
# see https://www.boost.org/doc/libs/1_70_0/doc/html/stacktrace/configuration_and_build.html#stacktrace.configuration_and_build.f3

backtrace_file=$(find /usr/lib/gcc/ -name 'backtrace.h' | head -n 1) && test -f $backtrace_file && ln -s $backtrace_file /tmp/backtrace.h