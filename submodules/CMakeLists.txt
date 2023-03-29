# Suppress all warnings for building the submodules (3rd party code)
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /w")
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything")
elseif (${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything")
endif()

# Boost
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
        serialization
        smart_ptr
        spirit
        stacktrace
        static_assert
        static_string
        system
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

foreach(lib IN LISTS BOOST_MODULE_LIBS)
    add_subdirectory(boost/libs/${lib} EXCLUDE_FROM_ALL)
endforeach()
include_directories(${BOOST_LIBRARY_INCLUDES})
add_library(Boost::stacktrace ALIAS boost_stacktrace_basic)

# Workaround for missing reference errata in the boost property_tree module
target_link_libraries(boost_property_tree INTERFACE Boost::any)
target_link_libraries(boost_property_tree INTERFACE Boost::format)
target_link_libraries(boost_property_tree INTERFACE Boost::multi_index)

# diskhash
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(diskhash STATIC
            diskhash/src/diskhash.c)
    include_directories(diskhash/src)
endif()

# RocksDB
include_directories(rocksdb/include)
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
        ${NANO_ROCKSDB_TOOLS}
        CACHE BOOL "" FORCE)
set(WITH_TOOLS
        ${NANO_ROCKSDB_TOOLS}
        CACHE BOOL "" FORCE)
if(ENABLE_AVX2)
    set(PORTABLE
            OFF
            CACHE BOOL "" FORCE)
else()
    set(PORTABLE
            ON
            CACHE BOOL "" FORCE)
endif()
set(FAIL_ON_WARNINGS
        OFF
        CACHE BOOL "") # Ignore unreachable code warning in merging_iterator.cc
# RocksDB v7.8.3 on Windows
# https://github.com/facebook/rocksdb/issues/11072
add_subdirectory(rocksdb EXCLUDE_FROM_ALL)

# cpptoml
include_directories(cpptoml/include)

## magic_enum
include_directories(magic_enum/include)

# UPNP
set(UPNPC_BUILD_SHARED
        OFF
        CACHE BOOL "")
add_subdirectory(miniupnp/miniupnpc EXCLUDE_FROM_ALL)

# CryptoPP
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
        AND (NANO_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS))
    set(CRYPTOPP_EXTRA
            cryptopp/crc_simd.cpp cryptopp/gcm_simd.cpp
            cryptopp/gf2n_simd.cpp cryptopp/neon_simd.cpp)
    add_definitions(-DCRYPTOPP_NO_CPU_FEATURE_PROBES)
endif()
# Some Clang cannot handle mixed asm with positional arguments, where the body
# is Intel style with no prefix and the templates are AT&T style. See:
# cryptopp/config.h Also see
# https://bugs.llvm.org/show_bug.cgi?id=39895
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_definitions(-DCRYPTOPP_DISABLE_MIXED_ASM -DCRYPTOPP_DISABLE_ASM)
    message(
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
        cryptopp/aes.h
        cryptopp/algparam.cpp
        cryptopp/allocate.cpp
        cryptopp/asn.cpp
        cryptopp/basecode.cpp
        cryptopp/cpu.cpp
        cryptopp/cryptlib.cpp
        cryptopp/default.cpp
        cryptopp/des.cpp
        cryptopp/dessp.cpp
        cryptopp/dll.cpp
        cryptopp/ec2n.cpp
        cryptopp/ecp.cpp
        cryptopp/filters.cpp
        cryptopp/fips140.cpp
        cryptopp/gcm.cpp
        cryptopp/gf2n.cpp
        cryptopp/gfpcrypt.cpp
        cryptopp/hex.cpp
        cryptopp/hmac.cpp
        cryptopp/hrtimer.cpp
        cryptopp/integer.cpp
        cryptopp/iterhash.cpp
        cryptopp/misc.h
        cryptopp/misc.cpp
        cryptopp/modes.h
        cryptopp/modes.cpp
        cryptopp/mqueue.cpp
        cryptopp/nbtheory.cpp
        cryptopp/oaep.cpp
        cryptopp/osrng.h
        cryptopp/osrng.cpp
        cryptopp/pubkey.cpp
        cryptopp/queue.cpp
        cryptopp/randpool.cpp
        cryptopp/rdtables.cpp
        cryptopp/rijndael.cpp
        cryptopp/rijndael_simd.cpp
        cryptopp/rng.cpp
        cryptopp/sha.cpp
        cryptopp/sha_simd.cpp
        cryptopp/simple.cpp
        cryptopp/sse_simd.cpp
        cryptopp/seckey.h
        cryptopp/siphash.h
        cryptopp/words.h
        ${CRYPTOPP_EXTRA})

if(MSVC)
    target_compile_options(cryptopp PRIVATE /w)
endif()

target_include_directories(cryptopp PUBLIC ${CMAKE_SOURCE_DIR}/submodules)

# argon2
if(WIN32 OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
    set(ARGON_CORE phc-winner-argon2/src/opt.c)
else()
    set(ARGON_CORE phc-winner-argon2/src/ref.c)
endif()

add_library(
        argon2
        phc-winner-argon2/src/argon2.c
        phc-winner-argon2/include/argon2.h
        phc-winner-argon2/src/core.c
        phc-winner-argon2/src/thread.c
        phc-winner-argon2/src/encoding.c
        ${ARGON_CORE})

target_include_directories(argon2 PUBLIC phc-winner-argon2/include)
target_include_directories(argon2 PUBLIC phc-winner-argon2/src)
target_include_directories(argon2 PUBLIC ${CMAKE_SOURCE_DIR}/crypto/blake2)

# LMDB
add_library(
        lmdb
        lmdb/libraries/liblmdb/lmdb.h
        lmdb/libraries/liblmdb/mdb.c
        lmdb/libraries/liblmdb/midl.c)

if(MSVC)
    target_link_libraries(lmdb ntdll)
    target_compile_options(lmdb PRIVATE /w)
endif()

# Blake2
if(WIN32)
    set(BLAKE2_IMPLEMENTATION "${CMAKE_SOURCE_DIR}/crypto/blake2/blake2b.c")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
        set(BLAKE2_IMPLEMENTATION "${CMAKE_SOURCE_DIR}/crypto/blake2/blake2b.c")
    else()
        set(BLAKE2_IMPLEMENTATION "${CMAKE_SOURCE_DIR}/crypto/blake2/blake2b-ref.c")
    endif()
endif()

add_library(blake2
        ${CMAKE_SOURCE_DIR}/crypto/blake2/blake2-config.h
        ${CMAKE_SOURCE_DIR}/crypto/blake2/blake2-impl.h
        ${CMAKE_SOURCE_DIR}/crypto/blake2/blake2.h
        ${BLAKE2_IMPLEMENTATION})

target_compile_definitions(blake2 PRIVATE -D__SSE2__)

# GoogleTest
if(NANO_TEST OR RAIBLOCKS_TEST)
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

    # FIXME: This fixes googletest GOOGLETEST_VERSION requirement
    set(GOOGLETEST_VERSION 1.11.0)
    add_subdirectory(gtest/googletest)
endif()

