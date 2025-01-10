cmake_minimum_required(VERSION 3.14)

if(CMAKE_VERSION VERSION_GREATER 3.12 OR CMAKE_VERSION VERSION_EQUAL 3.12)
  # find_package uses <PACKAGENAME>_ROOT variables
  cmake_policy(SET CMP0074 NEW)
endif()
if(CMAKE_VERSION VERSION_GREATER 3.13 OR CMAKE_VERSION VERSION_EQUAL 3.13)
  # option honors normal variables
  cmake_policy(SET CMP0077 NEW)
endif()

# OSX compatibility needs to be set before project is declared
set(CMAKE_OSX_DEPLOYMENT_TARGET
    13.3
    CACHE STRING "")

project(celerix-node)

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

option(CI_VERSION_PRE_RELEASE false)

set(COVERAGE
    OFF
    CACHE BOOL "Code Coverage Targets")
if(COVERAGE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")
  include(CoverageTest)
endif()

if(MSVC)
  add_definitions(/MP)
  add_definitions(
    -D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING) # Suppress iterator warning
endif()

set(CPACK_PACKAGE_VENDOR "Celerix Currency")
set(CPACK_PACKAGE_VERSION_MAJOR "28")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")

if(CI_VERSION_PRE_RELEASE)
  set(CPACK_PACKAGE_VERSION_PRE_RELEASE "${CI_VERSION_PRE_RELEASE}")
else()
  set(CPACK_PACKAGE_VERSION_PRE_RELEASE "99")
endif()

if(CI_TAG)
  set(TAG_VERSION_STRING "${CI_TAG}")
else()
  set(TAG_VERSION_STRING
      "V${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
endif()

if(APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
else()
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

# Create all libraries and executables in the root binary dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

set(CELERIX_GUI
    OFF
    CACHE BOOL "")
set(CELERIX_TEST
    OFF
    CACHE BOOL "")
set(CELERIX_SECURE_RPC
    OFF
    CACHE BOOL "")
set(CELERIX_WARN_TO_ERR
    OFF
    CACHE BOOL "")
set(CELERIX_TIMED_LOCKS
    0
    CACHE STRING "")
set(CELERIX_TIMED_LOCKS_IGNORE_BLOCKED
    OFF
    CACHE BOOL "")
set(CELERIX_TIMED_LOCKS_FILTER
    ""
    CACHE
      STRING
      "Selects which mutex should be the only one to have stacktraces generated, empty string means report on all"
)
set_property(
  CACHE CELERIX_TIMED_LOCKS_FILTER
  PROPERTY STRINGS
           active
           block_arrival
           block_processor
           block_uniquer
           dropped_elections,
           election_winner_details
           gap_cache
           network_filter
           observer_set
           request_aggregator
           state_block_signature_verification
           telemetry
           vote_generator
           vote_processor
           vote_uniquer
           votes_cache
           work_pool)
set(CELERIX_FUZZER_TEST
    OFF
    CACHE BOOL "")
set(CELERIX_ASIO_HANDLER_TRACKING
    OFF
    CACHE BOOL "")
set(CELERIX_ROCKSDB_TOOLS
    OFF
    CACHE BOOL "")

option(CELERIX_STACKTRACE_BACKTRACE
       "Use BOOST_STACKTRACE_USE_BACKTRACE in stacktraces, for POSIX" OFF)

if(CELERIX_STACKTRACE_BACKTRACE)
  add_definitions(-DCELERIX_STACKTRACE_BACKTRACE)
  add_definitions(-DBOOST_STACKTRACE_USE_BACKTRACE)
  if(CELERIX_BACKTRACE_INCLUDE)
    add_definitions(
      -DBOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE=${CELERIX_BACKTRACE_INCLUDE})
  endif()
endif()

# Enable CELERIX_TRACING by default in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  option(CELERIX_TRACING "Enable trace logging" ON)
else()
  option(CELERIX_TRACING "Enable trace logging" OFF)
endif()
if(CELERIX_TRACING)
  message(STATUS "Using trace logging")
  add_compile_definitions(CELERIX_TRACING)
endif()

if(${CELERIX_TIMED_LOCKS} GREATER 0)
  add_definitions(-DCELERIX_TIMED_LOCKS=${CELERIX_TIMED_LOCKS})
  add_definitions(-DCELERIX_TIMED_LOCKS_FILTER=${CELERIX_TIMED_LOCKS_FILTER})
  if(CELERIX_TIMED_LOCKS_IGNORE_BLOCKED)
    add_definitions(-DCELERIX_TIMED_LOCKS_IGNORE_BLOCKED)
  endif()
endif()

if(CELERIX_ASIO_HANDLER_TRACKING)
  add_definitions(-DBOOST_ASIO_ENABLE_HANDLER_TRACKING)
endif()

option(CELERIX_SIMD_OPTIMIZATIONS
       "Enable CPU-specific SIMD optimizations (SSE/AVX or NEON, e.g.)" OFF)
option(
  ENABLE_AES
  "Enable AES optimizations (enabled by default with CELERIX_SIMD_OPTIMIZATIONS, set OFF to disable"
  ON)
option(ENABLE_AVX2 "Enable AVX2 optimizations" OFF)

set(ACTIVE_NETWORK
    celerix_live_network
    CACHE STRING "Selects which network parameters are used")
set_property(
  CACHE ACTIVE_NETWORK PROPERTY STRINGS celerix_dev_network celerix_beta_network
                                celerix_live_network celerix_test_network)
set(CPACK_PACKAGE_NAME
    "celerix-node"
    CACHE STRING "" FORCE)
set(CPACK_NSIS_PACKAGE_NAME
    "Celerix"
    CACHE STRING "" FORCE)
set(CPACK_PACKAGE_INSTALL_DIRECTORY
    "celerixcurrency"
    CACHE STRING "" FORCE)
set(CELERIX_SERVICE "celerixcurrency.service")
set(CELERIX_PREFIX "")
if("${ACTIVE_NETWORK}" MATCHES "celerix_beta_network")
  project("celerix-node-beta")
  set(CPACK_PACKAGE_NAME
      "celerix-node-beta"
      CACHE STRING "" FORCE)
  set(CPACK_NSIS_PACKAGE_NAME
      "Celerix-Beta"
      CACHE STRING "" FORCE)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY
      "celerixcurrency-beta"
      CACHE STRING "" FORCE)
  set(CELERIX_SERVICE "celerixcurrency-beta.service")
  set(CELERIX_PREFIX "Beta")
elseif("${ACTIVE_NETWORK}" MATCHES "celerix_test_network")
  project("celerix-node-test")
  set(CPACK_PACKAGE_NAME
      "celerix-node-test"
      CACHE STRING "" FORCE)
  set(CPACK_NSIS_PACKAGE_NAME
      "Celerix-Test"
      CACHE STRING "" FORCE)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY
      "celerixcurrency-test"
      CACHE STRING "" FORCE)
  set(CELERIX_SERVICE "celerixcurrency-test.service")
  set(CELERIX_PREFIX "Test")
endif()
set(CELERIX_OSX_PACKAGE_NAME
    ${CPACK_NSIS_PACKAGE_NAME}
    CACHE STRING "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE
        STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads)

# Sanitizers
option(CELERIX_ASAN "Enable ASan" OFF)
if(CELERIX_ASAN)
  if(MSVC)
    message(FATAL_ERROR "ASan is not supported on MSVC")
  endif()

  message(STATUS "Using Asan")
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
endif()

option(CELERIX_ASAN_INT "Enable ASan+Integer overflow" OFF)
if(CELERIX_ASAN_INT)
  if(MSVC)
    message(FATAL_ERROR "ASan+Integer overflow is not supported on MSVC")
  endif()

  message(STATUS "Using ASan+Integer overflow")
  add_compile_options(-fsanitize=address,integer)
  add_link_options(-fsanitize=address,integer)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_asan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
endif()

option(CELERIX_UBSAN "Enable UBSan" OFF)
if(CELERIX_UBSAN)
  if(MSVC)
    message(FATAL_ERROR "UBSan is not supported on MSVC")
  endif()

  message(STATUS "Using UBSan")
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_ubsan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_ubsan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
  add_definitions(-DROCKSDB_UBSAN_RUN)
endif()

option(CELERIX_TSAN "Enable TSan" OFF)
if(CELERIX_TSAN)
  if(MSVC)
    message(FATAL_ERROR "TSan is not supported on MSVC")
  endif()

  message(STATUS "Using TSan")
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_tsan")
    add_link_options(
      "-fsanitize-ignorelist=${PROJECT_SOURCE_DIR}/sanitize_ignorelist_tsan")
  endif()

  add_definitions(-DED25519_NO_INLINE_ASM)
endif()

if(CELERIX_WARN_TO_ERR)
  add_compile_options(-Werror -Wno-deprecated-declarations)
endif()

if(WIN32)
  find_library(PSAPI Psapi)
  add_definitions(-D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -DMINIUPNP_STATICLIB
                  -D_CRT_SECURE_NO_WARNINGS -DNOGDI /EHsc)
else()
  if(CELERIX_FUZZER_TEST)
    add_compile_options(-fsanitize=fuzzer-no-link -fno-omit-frame-pointer)
    add_definitions(-DCELERIX_FUZZER_TEST)
  endif()

  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
    if(CELERIX_SIMD_OPTIMIZATIONS
       OR RAIBLOCKS_SIMD_OPTIMIZATIONS
       OR ENABLE_AVX2)
      add_compile_options(-msse4)
      if(ENABLE_AES)
        add_compile_options(-maes)
      else()
        add_definitions(-DCRYPTOPP_DISABLE_AESNI)
      endif()
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
      add_compile_options(-msse2)
      add_definitions(-DCRYPTOPP_DISABLE_SSSE3 -DCRYPTOPP_DISABLE_AESNI)
    endif()

  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    if(CELERIX_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS)
      add_compile_options(-march=armv8-a+crc+crypto)
    else()
      add_compile_options(-march=armv8-a)
      add_definitions(-DCRYPTOPP_DISABLE_ASM)
    endif()
  endif()
endif()

if(ENABLE_AVX2)
  if(WIN32)
    add_definitions(/arch:AVX2)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
    add_compile_options(-mavx2 -mbmi -mbmi2 -maes)
  endif()
  if(PERMUTE_WITH_GATHER)
    add_definitions(-DPERMUTE_WITH_GATHER)
  elseif(PERMUTE_WITH_SHUFFLES)
    add_definitions(-DPERMUTE_WITH_SHUFFLES)
  endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  set(PLATFORM_LINK_FLAGS "")
  add_definitions(/bigobj)
else()
  if(APPLE)
    set(PLATFORM_LINK_FLAGS "-framework Foundation -framework OpenCL")
  else()
    set(PLATFORM_LINK_FLAGS "-static-libgcc -static-libstdc++")
  endif()

  if(CELERIX_FUZZER_TEST)
    set(PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=fuzzer-no-link")
  endif()
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${PLATFORM_LINK_FLAGS}")

if(CELERIX_SECURE_RPC OR RAIBLOCKS_SECURE_RPC)
  find_package(OpenSSL 1.0 REQUIRED)
  include_directories(${OPENSSL_INCLUDE_DIR})
  add_definitions(-DCELERIX_SECURE_RPC)
  message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
  message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
  message("OpenSSL lib: ${OPENSSL_SSL_LIBRARY}")
  message("Crypto lib: ${OPENSSL_CRYPTO_LIBRARY}")
else()
  set(OPENSSL_LIBRARIES "")
endif()

include_directories(${CMAKE_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(Boost_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/submodules/boost/libs/config/include)
set(BOOST_MODULE_LIBS
    algorithm
    align
    any
    array
    asio
    assert
    atomic
    beast
    bind
    chrono
    circular_buffer
    concept_check
    config
    container
    container_hash
    context
    conversion
    core
    coroutine
    date_time
    describe
    detail
    dll
    dynamic_bitset
    endian
    exception
    filesystem
    foreach
    format
    function
    function_types
    functional
    fusion
    integer
    interprocess
    intrusive
    io
    iostreams
    iterator
    lexical_cast
    property_tree
    lambda
    log
    logic
    math
    move
    mp11
    mpl
    multi_index
    multiprecision
    numeric/conversion
    optional
    parameter
    phoenix
    pool
    predef
    preprocessor
    process
    program_options
    proto
    random
    range
    ratio
    rational
    regex
    scope
    scope_exit
    serialization
    smart_ptr
    spirit
    stacktrace
    static_assert
    static_string
    system
    test
    thread
    throw_exception
    tokenizer
    tuple
    type_index
    type_traits
    typeof
    unordered
    utility
    variant
    variant2
    winapi)

add_definitions(-DBOOST_ALL_NO_LIB) # Disable automatic boost linking
foreach(lib IN LISTS BOOST_MODULE_LIBS)
  add_subdirectory(submodules/boost/libs/${lib} EXCLUDE_FROM_ALL)
endforeach()
include_directories(${BOOST_LIBRARY_INCLUDES})
add_library(Boost::stacktrace ALIAS boost_stacktrace_basic)
add_definitions(-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)

# Workaround for missing reference errata in the boost property_tree module
target_link_libraries(boost_property_tree INTERFACE Boost::any)
target_link_libraries(boost_property_tree INTERFACE Boost::format)
target_link_libraries(boost_property_tree INTERFACE Boost::multi_index)

# RocksDB
include_directories(submodules/rocksdb/include)
set(USE_RTTI
    ON
    CACHE BOOL "")
set(WITH_GFLAGS
    OFF
    CACHE BOOL "")
set(WITH_TESTS
    OFF
    CACHE BOOL "")
set(WITH_BENCHMARK_TOOLS
    OFF
    CACHE BOOL "")
set(ROCKSDB_BUILD_SHARED
    OFF
    CACHE BOOL "")
set(WITH_CORE_TOOLS
    ${CELERIX_ROCKSDB_TOOLS}
    CACHE BOOL "" FORCE)
set(WITH_TOOLS
    ${CELERIX_ROCKSDB_TOOLS}
    CACHE BOOL "" FORCE)
if(ENABLE_AVX2)
  set(PORTABLE
      0
      CACHE BOOL "" FORCE)
else()
  set(PORTABLE
      1
      CACHE BOOL "" FORCE)
endif()
add_subdirectory(submodules/rocksdb EXCLUDE_FROM_ALL)

# cpptoml
include_directories(submodules/cpptoml/include)

# magic_enum
include_directories(submodules/magic_enum/include/magic_enum)

add_subdirectory(crypto/ed25519-donna)

add_subdirectory(celerix/ipc_flatbuffers_lib)
add_subdirectory(celerix/ipc_flatbuffers_test)

# fmt
add_subdirectory(submodules/fmt EXCLUDE_FROM_ALL)
include_directories(submodules/fmt/include)

# spdlog
add_definitions(-DSPDLOG_FMT_EXTERNAL)
add_subdirectory(submodules/spdlog EXCLUDE_FROM_ALL)
include_directories(submodules/spdlog/include)

# miniupnp
set(UPNPC_BUILD_SHARED
    OFF
    CACHE BOOL "")
add_subdirectory(submodules/miniupnp/miniupnpc EXCLUDE_FROM_ALL)

set(BUILD_SHARED
    OFF
    CACHE BOOL "")
set(BUILD_TESTING
    OFF
    CACHE BOOL "")
set(USE_INTERMEDIATE_OBJECTS_TARGET
    OFF
    CACHE BOOL "")
set(CRYPTOPP_EXTRA "")
if(WIN32)
  add_definitions(-DCRYPTOPP_DISABLE_ASM -DCRYPTOPP_DISABLE_SSSE3
                  -DCRYPTOPP_DISABLE_AESNI)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"
       AND (CELERIX_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS))
  set(CRYPTOPP_EXTRA
      submodules/cryptopp/crc_simd.cpp submodules/cryptopp/gcm_simd.cpp
      submodules/cryptopp/gf2n_simd.cpp submodules/cryptopp/neon_simd.cpp)
  add_definitions(-DCRYPTOPP_NO_CPU_FEATURE_PROBES)
endif()
# Some Clang cannot handle mixed asm with positional arguments, where the body
# is Intel style with no prefix and the templates are AT&T style. See:
# submodules/cryptopp/config.h Also see
# https://bugs.llvm.org/show_bug.cgi?id=39895
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_definitions(-DCRYPTOPP_DISABLE_MIXED_ASM -DCRYPTOPP_DISABLE_ASM)
  message(
    STATUS
      "CryptoPP with disabled ASM for ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  )
endif()
add_definitions(-DCRYPTOPP_DISABLE_SHANI)
# Fix failing builds after commit
# https://github.com/weidai11/cryptopp/commit/df9fa62205f2d341e2b1b26595a3a1b6377c60c5
add_definitions(-DCRYPTOPP_DISABLE_CLMUL)

set(CRYPTOPP_LIBRARY cryptopp)
add_library(
  cryptopp
  submodules/cryptopp/aes.h
  submodules/cryptopp/algparam.cpp
  submodules/cryptopp/allocate.cpp
  submodules/cryptopp/asn.cpp
  submodules/cryptopp/basecode.cpp
  submodules/cryptopp/cpu.cpp
  submodules/cryptopp/cryptlib.cpp
  submodules/cryptopp/default.cpp
  submodules/cryptopp/des.cpp
  submodules/cryptopp/dessp.cpp
  submodules/cryptopp/dll.cpp
  submodules/cryptopp/ec2n.cpp
  submodules/cryptopp/ecp.cpp
  submodules/cryptopp/filters.cpp
  submodules/cryptopp/fips140.cpp
  submodules/cryptopp/gcm.cpp
  submodules/cryptopp/gf2n.cpp
  submodules/cryptopp/gfpcrypt.cpp
  submodules/cryptopp/hex.cpp
  submodules/cryptopp/hmac.cpp
  submodules/cryptopp/hrtimer.cpp
  submodules/cryptopp/integer.cpp
  submodules/cryptopp/iterhash.cpp
  submodules/cryptopp/misc.h
  submodules/cryptopp/misc.cpp
  submodules/cryptopp/modes.h
  submodules/cryptopp/modes.cpp
  submodules/cryptopp/mqueue.cpp
  submodules/cryptopp/nbtheory.cpp
  submodules/cryptopp/oaep.cpp
  submodules/cryptopp/osrng.h
  submodules/cryptopp/osrng.cpp
  submodules/cryptopp/pubkey.cpp
  submodules/cryptopp/queue.cpp
  submodules/cryptopp/randpool.cpp
  submodules/cryptopp/rdtables.cpp
  submodules/cryptopp/rijndael.cpp
  submodules/cryptopp/rijndael_simd.cpp
  submodules/cryptopp/rng.cpp
  submodules/cryptopp/sha.cpp
  submodules/cryptopp/sha_simd.cpp
  submodules/cryptopp/simple.cpp
  submodules/cryptopp/sse_simd.cpp
  submodules/cryptopp/seckey.h
  submodules/cryptopp/siphash.h
  submodules/cryptopp/words.h
  ${CRYPTOPP_EXTRA})

target_include_directories(cryptopp PUBLIC submodules)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
  set(ARGON_CORE submodules/phc-winner-argon2/src/opt.c)
else()
  set(ARGON_CORE submodules/phc-winner-argon2/src/ref.c)
endif()

add_library(
  argon2
  submodules/phc-winner-argon2/src/argon2.c
  submodules/phc-winner-argon2/include/argon2.h
  submodules/phc-winner-argon2/src/core.c
  submodules/phc-winner-argon2/src/thread.c
  submodules/phc-winner-argon2/src/encoding.c
  ${ARGON_CORE})

target_include_directories(argon2 PUBLIC submodules/phc-winner-argon2/include)
target_include_directories(argon2 PUBLIC submodules/phc-winner-argon2/src)
target_include_directories(argon2 PUBLIC crypto/blake2)

add_library(
  lmdb
  submodules/lmdb/libraries/liblmdb/lmdb.h
  submodules/lmdb/libraries/liblmdb/mdb.c
  submodules/lmdb/libraries/liblmdb/midl.c)

if(WIN32)
  target_link_libraries(lmdb ntdll)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
  set(BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b.c")
else()
  set(BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b-ref.c")
endif()

add_library(blake2 crypto/blake2/blake2-config.h crypto/blake2/blake2-impl.h
                   crypto/blake2/blake2.h ${BLAKE2_IMPLEMENTATION})

target_compile_definitions(blake2 PRIVATE -D__SSE2__)

add_subdirectory(celerix/crypto_lib)
add_subdirectory(celerix/secure)
add_subdirectory(celerix/store)
target_include_directories(celerix_store PUBLIC ${BOOST_LIBRARY_INCLUDES})
target_include_directories(celerix_store PUBLIC submodules)
add_subdirectory(celerix/lib)
add_subdirectory(celerix/node)
add_subdirectory(celerix/celerix_node)
add_subdirectory(celerix/rpc)
add_subdirectory(celerix/celerix_rpc)

add_custom_target(
  executables
  COMMAND echo "BATCH BUILDING node + rpc"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS celerix_node celerix_rpc)

if(CELERIX_FUZZER_TEST)
  if(NOT WIN32)
    add_subdirectory(celerix/fuzzer_test)
  else()
    message(WARNING "Fuzzing is not supported on Windows")
  endif()
endif()

if(CELERIX_TEST OR RAIBLOCKS_TEST)
  if(WIN32)
    if(MSVC_VERSION)
      if(MSVC_VERSION GREATER_EQUAL 1910)
        add_definitions(-DGTEST_LANG_CXX11=1)
        add_definitions(-DGTEST_HAS_TR1_TUPLE=0)
      endif()
    endif()
    set(gtest_force_shared_crt ON)
  else()
    set(gtest_force_shared_crt OFF)
  endif()

  add_subdirectory(celerix/load_test)

  # FIXME: This fixes googletest GOOGLETEST_VERSION requirement
  set(GOOGLETEST_VERSION 1.11.0)
  add_subdirectory(submodules/gtest/googletest)
  # FIXME: This fixes gtest include directories without modifying gtest's
  # CMakeLists.txt. Ideally we should use GTest::GTest and GTest::Main as
  # dependencies but it requires building gtest differently
  set_target_properties(
    gtest PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                     "${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include")

  add_subdirectory(celerix/test_common)
  add_subdirectory(celerix/core_test)
  add_subdirectory(celerix/rpc_test)
  add_subdirectory(celerix/slow_test)
  add_custom_target(
    all_tests
    COMMAND echo "BATCH BUILDING TESTS"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS core_test load_test rpc_test slow_test celerix_node celerix_rpc)
endif()

if(CELERIX_TEST OR RAIBLOCKS_TEST)
  if(CELERIX_GUI OR RAIBLOCKS_GUI)
    add_custom_target(
      quick_tests
      COMMAND echo "BATCH QUICK TESTS"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS core_test rpc_test qt_test)
  else()
    add_custom_target(
      quick_tests
      COMMAND echo "BATCH QUICK TESTS -- qt_test is disabled"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS core_test rpc_test)
  endif()
endif()

if(CELERIX_GUI OR RAIBLOCKS_GUI)
  install(FILES ${PROJECT_BINARY_DIR}/config-node.toml.sample DESTINATION .)
  install(FILES ${PROJECT_BINARY_DIR}/config-rpc.toml.sample DESTINATION .)
  install(FILES ${PROJECT_BINARY_DIR}/config-log.toml.sample DESTINATION .)
  if(WIN32)
    set(PLATFORM_QT_PACKAGES WinExtras)
  else()
    set(PLATFORM_QT_PACKAGES)
  endif()

  find_package(Qt5 COMPONENTS Core Gui Widgets Test ${PLATFORM_QT_PACKAGES})

  add_library(qt celerix/qt/qt.cpp celerix/qt/qt.hpp)
  include_directories(${CMAKE_SOURCE_DIR}/submodules)

  target_link_libraries(
    qt
    node
    secure
    celerix_lib
    libminiupnpc-static
    Qt5::Gui
    Qt5::Widgets)

  target_compile_definitions(
    qt PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
               -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

  if(WIN32)
    set(PLATFORM_GUI_TARGET_PROPERTIES WIN32)
  else()
    set(PLATFORM_GUI_TARGET_PROPERTIES "")
  endif()

  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(PLATFORM_WALLET_SOURCE celerix/celerix_wallet/plat/default/icon.cpp)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(PLATFORM_WALLET_SOURCE celerix/celerix_wallet/plat/windows/icon.cpp Celerix.rc)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(PLATFORM_WALLET_SOURCE celerix/celerix_wallet/plat/default/icon.cpp)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    set(PLATFORM_WALLET_SOURCE celerix/celerix_wallet/plat/default/icon.cpp)
  else()
    error("Unknown platform: ${CMAKE_SYSTEM_NAME}")
  endif()

  qt5_add_resources(RES resources.qrc)

  add_executable(
    celerix_wallet ${PLATFORM_GUI_TARGET_PROPERTIES} ${PLATFORM_WALLET_SOURCE}
                celerix/celerix_wallet/entry.cpp celerix/celerix_wallet/icon.hpp ${RES})

  target_link_libraries(celerix_wallet Boost::process rpc node qt)

  if(WIN32)
    target_link_libraries(celerix_wallet Qt5::WinExtras)
    # celerix_wallet.com executable for Windows console
    add_executable(celerix_wallet_com celerix/celerix_wallet/entry_com.cpp)
    target_link_libraries(celerix_wallet_com node)
    set_target_properties(
      celerix_wallet_com
      PROPERTIES COMPILE_FLAGS "-DBOOST_ASIO_HAS_STD_ARRAY=1"
                 OUTPUT_NAME "celerix_wallet"
                 SUFFIX ".com")
  endif()

  set_target_properties(
    qt celerix_wallet PROPERTIES COMPILE_FLAGS
                              "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

  if(CELERIX_TEST OR RAIBLOCKS_TEST)
    add_executable(qt_test celerix/qt_test/entry.cpp celerix/qt_test/qt.cpp)

    target_link_libraries(
      qt_test
      node
      secure
      test_common
      gtest
      gtest_main
      qt
      Qt5::Test)

    set_target_properties(
      qt_test PROPERTIES COMPILE_FLAGS
                         "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")
    add_dependencies(all_tests qt_test)
  endif()

  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    configure_file(${CMAKE_SOURCE_DIR}/Info.plist.in
                   ${CMAKE_SOURCE_DIR}/Info.plist @ONLY)
    install(TARGETS celerix_wallet
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/MacOS)
    install(TARGETS celerix_node
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/MacOS)
    install(TARGETS celerix_rpc
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/MacOS)
    install(FILES Info.plist DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents)
    install(FILES qt.conf
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Resources)
    install(DIRECTORY ${Qt5_DIR}/../../QtCore.framework
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtDBus.framework
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtGui.framework
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtPrintSupport.framework
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtTest.framework
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(DIRECTORY ${Qt5_DIR}/../../QtWidgets.framework
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Frameworks)
    install(FILES "${Qt5_DIR}/../../../plugins/platforms/libqcocoa.dylib"
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/PlugIns/platforms)
    install(FILES Celerix.icns
            DESTINATION ${CELERIX_OSX_PACKAGE_NAME}.app/Contents/Resources)
  elseif(WIN32)
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
      set(WIN_REDIST vc_redist.x64.exe)
    else()
      set(WIN_REDIST vc_redist.x86.exe)
    endif()
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "ExecWait '\\\"$INSTDIR\\\\${WIN_REDIST}\\\" /quiet /norestart'
												WriteRegDWORD HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Celerix\\\\Celerix${CELERIX_PREFIX}' 'TypesSupported' '0x7'
												WriteRegStr HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Celerix\\\\Celerix${CELERIX_PREFIX}' 'EventMessageFile' '$INSTDIR\\\\celerix_wallet.exe'"
    )
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegKey HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Celerix${CELERIX_PREFIX}'"
    )
    set(CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/Celerix.ico)
    set(CPACK_NSIS_DISPLAY_NAME "Celerix${CELERIX_PREFIX}")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://celerix.org")
    set(CPACK_NSIS_CONTACT "info@celerix.org")
    set(CPACK_NSIS_MENU_LINKS "celerix_wallet.exe" "Celerix Wallet"
                              "https://celerix.org" "Celerix website")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    get_target_property(Qt5WindowsPlugin Qt5::QWindowsIntegrationPlugin
                        LOCATION)
    get_filename_component(Qt5_bin_DIR ${Qt5_DIR}/../../../bin ABSOLUTE)
    install(TARGETS celerix_wallet DESTINATION .)
    install(TARGETS celerix_wallet_com DESTINATION .)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${WIN_REDIST} DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/libGLESv2.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5Core.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5Gui.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5Widgets.dll DESTINATION .)
    install(FILES ${Qt5_bin_DIR}/Qt5WinExtras.dll DESTINATION .)
    install(FILES ${Qt5WindowsPlugin} DESTINATION platforms)
  else()
    set(CPACK_GENERATOR "TBZ2;DEB")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qt5-default | qtbase5-dev, qtchooser, qt5-qmake, qtbase5-dev-tools")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "russel@celerix.org")
    install(TARGETS celerix_wallet RUNTIME DESTINATION ./bin)
    set(DEBIAN_POSTINST postinst.in)
    set(DEBIAN_POSTRM postrm.in)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/debian-control/${DEBIAN_POSTINST}
                   ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postinst)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/debian-control/${DEBIAN_POSTRM}
                   ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postrm)
    file(
      COPY ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postinst
           ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/postrm
      DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/debian-control/.
      FILE_PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE)
    install(FILES etc/systemd/${CELERIX_SERVICE} DESTINATION ./extras/systemd/.)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_BINARY_DIR}/debian-control/postinst;${CMAKE_CURRENT_BINARY_DIR}/debian-control/postrm"
    )
  endif()
endif()

if(COVERAGE)
  add_subdirectory(coverage)
endif()

set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
include(CPack)
