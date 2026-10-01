# Included via CMAKE_PROJECT_rocksdb_INCLUDE. RocksDB hardcodes /Zi and
# /d2Zi+ for MSVC, which defeat compiler caching (shared PDB, and sccache
# rejects /d2Zi+ outright). Strip them once RocksDB has set its flags; /Z7
# elsewhere still gives it embedded debug info.

if(NOT MSVC)
  return()
endif()

if(NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  return()
endif()

# DEFER requires CMake 3.19; older just stays uncached.
if(CMAKE_VERSION VERSION_LESS 3.19)
  message(
    STATUS "RocksDB: CMake < 3.19, leaving MSVC debug flags uncacheable")
  return()
endif()

function(nano_strip_rocksdb_uncacheable_flags)
  foreach(flags_var CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    string(REPLACE "/d2Zi+" "" ${flags_var} "${${flags_var}}")
    string(REPLACE "/Zi" "" ${flags_var} "${${flags_var}}")
    set(${flags_var} "${${flags_var}}" PARENT_SCOPE)
  endforeach()
endfunction()

cmake_language(DEFER CALL nano_strip_rocksdb_uncacheable_flags)
