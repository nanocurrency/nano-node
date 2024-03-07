#!/bin/bash
#set -x
#Check out boost submodule with minimum set of modules to reduce size

# flattened boost libs dependency list
dependencies=("algorithm" "align" "any" "array" "asio" "assert" "atomic" "beast" "bind" "chrono" "circular_buffer" "concept_check" "config" "container" "container_hash" "context" "conversion" "core" "coroutine" "date_time" "describe" "detail" "dll" "dynamic_bitset" "endian" "exception" "filesystem" "foreach" "format" "function" "function_types" "functional" "fusion" "integer" "interprocess" "intrusive" "io" "iostreams" "iterator" "lexical_cast" "log" "logic" "math" "move" "mp11" "mpl" "multi_index" "multiprecision" "numeric_conversion" "optional" "parameter" "phoenix" "pool" "predef" "preprocessor" "process" "program_options" "property_tree" "proto" "random" "range" "ratio" "rational" "regex" "serialization" "smart_ptr" "spirit" "stacktrace" "static_assert" "static_string" "system" "thread" "throw_exception" "tokenizer" "tuple" "type_index" "type_traits" "typeof" "unordered" "utility" "variant" "variant2" "winapi")

git submodule init boost
cd boost
# deactivate all boost submodules
git submodule foreach 'git config submodule.$sm_path.active false'
# selectively activate required dependencies
for i in ${dependencies[@]}
do
  git config submodule.$i.active true
done
cd ..
# Update all submodules recursively. Deactivated modules will be skipped by --recursive
git submodule update --jobs 16 --recursive --recommend-shallow --single-branch
