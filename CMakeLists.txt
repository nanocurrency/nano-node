cmake_minimum_required (VERSION 3.4)
project (nano-node)

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

option (CI_BUILD false)

set (CPACK_PACKAGE_VERSION_MAJOR "20")
set (CPACK_PACKAGE_VERSION_MINOR "0")
set (CPACK_PACKAGE_VERSION_PATCH "0")
set (CPACK_PACKAGE_VENDOR "Nano Currency")

if (CI_BUILD)
	set (TAG_VERSION_STRING "$ENV{TRAVIS_TAG}")
else()
	set (TAG_VERSION_STRING "V${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
endif()

set (CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
# Create all libraries and executables in the root binary dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set (NANO_GUI OFF CACHE BOOL "")
set (NANO_TEST OFF CACHE BOOL "")
set (NANO_SECURE_RPC OFF CACHE BOOL "")
set (NANO_ROCKSDB OFF CACHE BOOL "")
set (NANO_WARN_TO_ERR OFF CACHE BOOL "")
set (NANO_TIMED_LOCKS 0 CACHE INTEGER "")

option (NANO_STACKTRACE_BACKTRACE "Use BOOST_STACKTRACE_USE_BACKTRACE in stacktraces, for POSIX" OFF)
if (NANO_STACKTRACE_BACKTRACE)
	add_definitions(-DNANO_STACKTRACE_BACKTRACE=1)
endif ()

if (${NANO_TIMED_LOCKS} GREATER 0)
	add_definitions (-DNANO_TIMED_LOCKS=${NANO_TIMED_LOCKS})
endif ()

if (NANO_ROCKSDB)
	add_definitions (-DNANO_ROCKSDB=1)
endif ()
option(NANO_ASAN_INT "Enable ASan+UBSan+Integer overflow" OFF)
option(NANO_ASAN "Enable ASan+UBSan" OFF)
option(NANO_TSAN "Enable TSan" OFF)
option(NANO_SIMD_OPTIMIZATIONS "Enable CPU-specific SIMD optimizations (SSE/AVX or NEON, e.g.)" OFF)
option(ENABLE_AES "Enable AES optimizations (enabled by default with NANO_SIMD_OPTIMIZATIONS, set OFF to disable" ON)
option(ENABLE_AVX2 "Enable AVX2 optimizations" OFF)

SET (ACTIVE_NETWORK nano_live_network CACHE STRING "Selects which network parameters are used")
set_property (CACHE ACTIVE_NETWORK PROPERTY STRINGS nano_test_network nano_beta_network nano_live_network)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(USING_ASAN (NANO_ASAN OR RAIBLOCKS_ASAN))
set(USING_ASAN_INT (NANO_ASAN_INT OR RAIBLOCKS_ASAN_INT))
set(USING_TSAN (NANO_TSAN OR RAIBLOCKS_TSAN))

find_package(Threads)

if (WIN32)
	find_library (PSAPI Psapi)
	add_definitions(-D_WIN32_WINNT=0x0600
					-DWINVER=0x0600
					-DWIN32_LEAN_AND_MEAN
					-DMINIUPNP_STATICLIB
					-D_CRT_SECURE_NO_WARNINGS)

	if (${USING_TSAN} OR ${USING_ASAN} OR ${USING_ASAN_INT})
		message (WARNING "Cannot use TSAN or ASAN on Windows, sanitizers ignored")
	endif()

else ()
	if (NANO_WARN_TO_ERR)
		add_compile_options(-Werror)
	endif ()

	if ((${USING_TSAN} AND ${USING_ASAN}) OR
	    (${USING_TSAN} AND ${USING_ASAN_INT}))
		message (WARNING "Cannot use TSAN/ASAN together, defaulting to ASAN")
	endif()

	if (${USING_ASAN} OR ${USING_ASAN_INT})
		if (${USING_ASAN_INT})
			add_compile_options(-fsanitize=address,undefined,integer)
		else ()	
			add_compile_options(-fsanitize=address,undefined)
		endif()
		add_definitions(-DED25519_NO_INLINE_ASM)
	elseif (${USING_TSAN})
		add_compile_options(-fsanitize=thread)
		if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			add_compile_options("-fsanitize-blacklist=${PROJECT_SOURCE_DIR}/tsan_clang_blacklist")
		endif()
		add_definitions(-DED25519_NO_INLINE_ASM)
	endif()

	if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
		if (NANO_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS OR ENABLE_AVX2)
			add_compile_options(-msse4)
			if (ENABLE_AES)
				add_compile_options(-maes)
			else ()
				add_definitions(-DCRYPTOPP_DISABLE_AESNI)
			endif ()
		elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
			add_compile_options(-msse2)
			add_definitions(-DCRYPTOPP_DISABLE_SSSE3
							-DCRYPTOPP_DISABLE_AESNI)
		endif()

	elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
		if (NANO_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS)
			add_compile_options(-march=armv8-a+crc+crypto)
		else ()
			add_compile_options(-march=armv8-a)
			add_definitions(-DCRYPTOPP_DISABLE_ASM)
		endif ()
	endif()
endif ()

if (ENABLE_AVX2)
	if (WIN32)
		add_definitions(/arch:AVX2)
	elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
		add_compile_options(-mavx2 -mbmi -mbmi2 -maes)
	endif()
	if (PERMUTE_WITH_GATHER)
		add_definitions(-DPERMUTE_WITH_GATHER)
	elseif (PERMUTE_WITH_SHUFFLES)
		add_definitions(-DPERMUTE_WITH_SHUFFLES)
	endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
#set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
	set (PLATFORM_LINK_FLAGS "") 
	add_definitions(/bigobj)
else ()
	if (APPLE)
		set (PLATFORM_LINK_FLAGS "-framework Foundation -framework OpenCL")
	else ()
		set (PLATFORM_LINK_FLAGS "-static-libgcc -static-libstdc++")
	endif ()

	if (${USING_ASAN_INT})
		set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=address,undefined,integer")
	elseif (${USING_ASAN})
		set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=address,undefined")
	elseif (${USING_TSAN})
		set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=thread")
		if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize-blacklist=${PROJECT_SOURCE_DIR}/tsan_clang_blacklist")
		endif()
	endif()
endif ()

SET( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${PLATFORM_LINK_FLAGS}" )

if (NANO_SECURE_RPC OR RAIBLOCKS_SECURE_RPC)
	find_package (OpenSSL 1.0 REQUIRED)
	include_directories(${OPENSSL_INCLUDE_DIR})
	add_definitions (-DNANO_SECURE_RPC)
	message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
	message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
	message("OpenSSL lib: ${OPENSSL_SSL_LIBRARY}")
	message("Crypto lib: ${OPENSSL_CRYPTO_LIBRARY}")
else ()
	set (OPENSSL_LIBRARIES "")
endif ()

include_directories (${CMAKE_SOURCE_DIR})

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

if (${NANO_TIMED_LOCKS} GREATER 0)
	set (timed_locks_boost_libs context fiber)
endif ()

find_package (Boost 1.67.0 REQUIRED COMPONENTS filesystem log log_setup thread program_options system ${timed_locks_boost_libs})

if (NANO_ROCKSDB)
	find_package (RocksDB REQUIRED)
	find_package (ZLIB REQUIRED)
	include_directories (${ROCKSDB_INCLUDE_DIRS})
endif ()

# There is a compile bug with boost 1.69 interprocess headers on Mac
if (APPLE AND Boost_VERSION EQUAL 106900)
	set (BOOST_PROCESS_SUPPORTED 0)
else ()
	set (BOOST_PROCESS_SUPPORTED 1)
endif ()

include_directories(cpptoml/include)
add_subdirectory(crypto/ed25519-donna)

set (UPNPC_BUILD_SHARED OFF CACHE BOOL "")
add_subdirectory (miniupnp/miniupnpc EXCLUDE_FROM_ALL)
# FIXME: This fixes miniupnpc include directories without modifying miniupnpc's
# CMakeLists.txt but should be set there
set_target_properties(libminiupnpc-static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
	"${CMAKE_SOURCE_DIR}/miniupnp/miniupnpc")

set (BUILD_SHARED OFF CACHE BOOL "")
set (BUILD_TESTING OFF CACHE BOOL "")
set (USE_INTERMEDIATE_OBJECTS_TARGET OFF CACHE BOOL "")
set (CRYPTOPP_EXTRA "")
if (WIN32)
	set (CRYPTOPP_EXTRA crypto/cryptopp/x64dll.asm
					crypto/cryptopp/x64masm.asm)
	enable_language(ASM)
	enable_language(ASM_MASM)
	# similar to SSE2 settings
	add_definitions(-DCRYPTOPP_DISABLE_SSSE3
					-DCRYPTOPP_DISABLE_AESNI)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND (NANO_SIMD_OPTIMIZATIONS OR RAIBLOCKS_SIMD_OPTIMIZATIONS))
	set (CRYPTOPP_EXTRA crypto/cryptopp/crc_simd.cpp
					crypto/cryptopp/gcm_simd.cpp
					crypto/cryptopp/gf2n_simd.cpp
					crypto/cryptopp/neon_simd.cpp)
	add_definitions(-DCRYPTOPP_NO_CPU_FEATURE_PROBES)
endif ()
# Some Clang cannot handle mixed asm with positional arguments, where the
# body is Intel style with no prefix and the templates are AT&T style.
# See: crypto/cryptopp/config.h
# Also see https://bugs.llvm.org/show_bug.cgi?id=39895
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	add_definitions(-DCRYPTOPP_DISABLE_MIXED_ASM -DCRYPTOPP_DISABLE_ASM)
	message("CryptoPP with disabled ASM for ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif ()
add_definitions(-DCRYPTOPP_DISABLE_SHANI)
# Fix failing builds after commit https://github.com/weidai11/cryptopp/commit/df9fa62205f2d341e2b1b26595a3a1b6377c60c5
add_definitions(-DCRYPTOPP_DISABLE_CLMUL)

set (CRYPTOPP_LIBRARY cryptopp)
add_library (cryptopp
	crypto/cryptopp/algparam.cpp
	crypto/cryptopp/asn.cpp
	crypto/cryptopp/basecode.cpp
	crypto/cryptopp/cpu.cpp
	crypto/cryptopp/cryptlib.cpp
	crypto/cryptopp/default.cpp
	crypto/cryptopp/des.cpp
	crypto/cryptopp/dessp.cpp
	crypto/cryptopp/dll.cpp
	crypto/cryptopp/ec2n.cpp
	crypto/cryptopp/ecp.cpp
	crypto/cryptopp/filters.cpp
	crypto/cryptopp/fips140.cpp
	crypto/cryptopp/gcm.cpp
	crypto/cryptopp/gf2n.cpp
	crypto/cryptopp/gfpcrypt.cpp
	crypto/cryptopp/hex.cpp
	crypto/cryptopp/hmac.cpp
	crypto/cryptopp/hrtimer.cpp
	crypto/cryptopp/integer.cpp
	crypto/cryptopp/iterhash.cpp
	crypto/cryptopp/misc.cpp
	crypto/cryptopp/modes.cpp
	crypto/cryptopp/mqueue.cpp
	crypto/cryptopp/nbtheory.cpp
	crypto/cryptopp/oaep.cpp
	crypto/cryptopp/osrng.cpp
	crypto/cryptopp/pubkey.cpp
	crypto/cryptopp/queue.cpp
	crypto/cryptopp/randpool.cpp
	crypto/cryptopp/rdtables.cpp
	crypto/cryptopp/rijndael.cpp
	crypto/cryptopp/rijndael_simd.cpp
	crypto/cryptopp/rng.cpp
	crypto/cryptopp/sha.cpp
	crypto/cryptopp/sha_simd.cpp
	crypto/cryptopp/simple.cpp
	crypto/cryptopp/sse_simd.cpp
	${CRYPTOPP_EXTRA})

if (WIN32 OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
	set (ARGON_CORE crypto/phc-winner-argon2/src/opt.c)
else ()
	set (ARGON_CORE crypto/phc-winner-argon2/src/ref.c)
endif ()

add_library (argon2
	crypto/phc-winner-argon2/src/argon2.c
	crypto/phc-winner-argon2/include/argon2.h
	crypto/phc-winner-argon2/src/core.c
	crypto/phc-winner-argon2/src/thread.c
	crypto/phc-winner-argon2/src/encoding.c
	${ARGON_CORE})

target_include_directories(argon2 PUBLIC crypto/phc-winner-argon2/include)
target_include_directories(argon2 PUBLIC crypto/phc-winner-argon2/src)
target_include_directories(argon2 PUBLIC crypto/blake2)

add_library (lmdb
	lmdb/libraries/liblmdb/lmdb.h
	lmdb/libraries/liblmdb/mdb.c
	lmdb/libraries/liblmdb/midl.c)

if (WIN32)
	target_link_libraries(lmdb ntdll)
endif()

if (WIN32)
	set (BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b.c")
else ()
	IF (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
		set (BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b.c")
	else()
		set (BLAKE2_IMPLEMENTATION "crypto/blake2/blake2b-ref.c")
	endif()
endif ()

add_library (blake2
	crypto/blake2/blake2-config.h
	crypto/blake2/blake2-impl.h
	crypto/blake2/blake2.h
	${BLAKE2_IMPLEMENTATION})

target_compile_definitions(blake2 PRIVATE -D__SSE2__)

add_subdirectory(nano/crypto_lib)
add_subdirectory(nano/secure)
add_subdirectory(nano/lib)
add_subdirectory(nano/node)
add_subdirectory(nano/nano_node)
add_subdirectory(nano/rpc)
add_subdirectory(nano/nano_rpc)

if (NANO_TEST OR RAIBLOCKS_TEST)
	if(WIN32)
		if(MSVC_VERSION)
			if(MSVC_VERSION GREATER_EQUAL 1910)
				add_definitions(-DGTEST_LANG_CXX11=1)
				add_definitions(-DGTEST_HAS_TR1_TUPLE=0)
			endif()
		endif()
		set (gtest_force_shared_crt ON)
	else ()
		set (gtest_force_shared_crt OFF)
endif()

	add_subdirectory(nano/load_test)

	add_subdirectory (gtest/googletest)
	# FIXME: This fixes gtest include directories without modifying gtest's
	# CMakeLists.txt. Ideally we should use GTest::GTest and GTest::Main as
	# dependencies but it requires building gtest differently
	set_target_properties(gtest PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
		"${CMAKE_SOURCE_DIR}/gtest/googletest/include")

	add_subdirectory(nano/core_test)
	add_subdirectory(nano/rpc_test)
	add_subdirectory(nano/slow_test)
endif ()

if (NANO_GUI OR RAIBLOCKS_GUI)
	if (WIN32)
		set (PLATFORM_QT_PACKAGES WinExtras)
	else ()
		set (PLATFORM_QT_PACKAGES)
	endif ()

	find_package (Qt5 COMPONENTS Core Gui Widgets Test ${PLATFORM_QT_PACKAGES})

	add_library (qt
		nano/qt/qt.cpp
		nano/qt/qt.hpp)

	target_link_libraries(qt
		node secure nano_lib libminiupnpc-static Qt5::Gui Qt5::Widgets)

	target_compile_definitions(qt
		PRIVATE
		-DTAG_VERSION_STRING=${TAG_VERSION_STRING}
		-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

	if (WIN32)
		set (PLATFORM_GUI_TARGET_PROPERTIES WIN32)
	else ()
		set (PLATFORM_GUI_TARGET_PROPERTIES "")
	endif ()

	if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		set (PLATFORM_WALLET_SOURCE nano/nano_wallet/plat/default/icon.cpp)
	elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		set (PLATFORM_WALLET_SOURCE nano/nano_wallet/plat/windows/icon.cpp Nano.rc)
	elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		set (PLATFORM_WALLET_SOURCE nano/nano_wallet/plat/default/icon.cpp)
	elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
		set (PLATFORM_WALLET_SOURCE nano/nano_wallet/plat/default/icon.cpp)
	else ()
		error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
	endif ()

	qt5_add_resources(RES resources.qrc)

	add_executable (nano_wallet ${PLATFORM_GUI_TARGET_PROPERTIES}
		${PLATFORM_WALLET_SOURCE}
		nano/nano_wallet/entry.cpp
		nano/nano_wallet/icon.hpp
		${RES})

	target_link_libraries (nano_wallet
		rpc
		node
		qt)

	target_compile_definitions(nano_wallet
		PRIVATE
			-DBOOST_PROCESS_SUPPORTED=${BOOST_PROCESS_SUPPORTED})

	if (WIN32)
		target_link_libraries (nano_wallet Qt5::WinExtras)
		# nano_wallet.com executable for Windows console
		add_executable(nano_wallet_com
			nano/nano_wallet/entry_com.cpp)
		target_link_libraries (nano_wallet_com
			node)
		set_target_properties (nano_wallet_com PROPERTIES COMPILE_FLAGS "-DBOOST_ASIO_HAS_STD_ARRAY=1" OUTPUT_NAME "nano_wallet" SUFFIX ".com")
	endif()

	add_executable (qt_system
		nano/qt_system/entry.cpp)

	target_link_libraries (qt_system qt node Qt5::Gui Qt5::Widgets)

	set_target_properties (qt nano_wallet qt_system PROPERTIES COMPILE_FLAGS "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

	if (NANO_TEST OR RAIBLOCKS_TEST)
		add_executable (qt_test
			nano/qt_test/entry.cpp
			nano/qt_test/qt.cpp)

		target_link_libraries(qt_test gtest gtest_main qt Qt5::Test)

		set_target_properties (qt_test PROPERTIES COMPILE_FLAGS "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")
	endif ()

	if (APPLE)
		set (CPACK_GENERATOR "DragNDrop")
		CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/Info.plist.in ${CMAKE_SOURCE_DIR}/Info.plist @ONLY)
		install (TARGETS nano_wallet DESTINATION Nano.app/Contents/MacOS)
		install (TARGETS nano_node DESTINATION Nano.app/Contents/MacOS)
		install (TARGETS nano_rpc DESTINATION Nano.app/Contents/MacOS)
		install (FILES Info.plist DESTINATION Nano.app/Contents)
		install (FILES qt.conf DESTINATION Nano.app/Contents/Resources)
		install (DIRECTORY ${Qt5_DIR}/../../QtCore.framework DESTINATION Nano.app/Contents/Frameworks)
		install (DIRECTORY ${Qt5_DIR}/../../QtDBus.framework DESTINATION Nano.app/Contents/Frameworks)
		install (DIRECTORY ${Qt5_DIR}/../../QtGui.framework DESTINATION Nano.app/Contents/Frameworks)
		install (DIRECTORY ${Qt5_DIR}/../../QtPrintSupport.framework DESTINATION Nano.app/Contents/Frameworks)
		install (DIRECTORY ${Qt5_DIR}/../../QtTest.framework DESTINATION Nano.app/Contents/Frameworks)
		install (DIRECTORY ${Qt5_DIR}/../../QtWidgets.framework DESTINATION Nano.app/Contents/Frameworks)
		install (FILES "${Qt5_DIR}/../../../plugins/platforms/libqcocoa.dylib" DESTINATION Nano.app/Contents/PlugIns/platforms)
		install (FILES Nano.icns DESTINATION Nano.app/Contents/Resources)
	elseif (WIN32)
		if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
			set (WIN_REDIST vc_redist.x64.exe)
		else ()
			set (WIN_REDIST vc_redist.x86.exe)
		endif ()
		set (CPACK_NSIS_EXTRA_INSTALL_COMMANDS "ExecWait '\\\"$INSTDIR\\\\${WIN_REDIST}\\\" /quiet /norestart'
												WriteRegDWORD HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Nano\\\\Nano' 'TypesSupported' '0x7'
												WriteRegStr HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Nano\\\\Nano' 'EventMessageFile' '$INSTDIR\\\\nano_wallet.exe'")
		set (CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "DeleteRegKey HKLM 'SYSTEM\\\\CurrentControlSet\\\\Services\\\\EventLog\\\\Nano'")
		set (CPACK_PACKAGE_INSTALL_DIRECTORY "nanocurrency")
		set (CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/Nano.ico)
		set (CPACK_NSIS_DISPLAY_NAME "Nano")
		set (CPACK_PACKAGE_NAME "nano-node")
		set (CPACK_NSIS_PACKAGE_NAME "Nano")
		set (CPACK_NSIS_URL_INFO_ABOUT "https://nano.org")
		set (CPACK_NSIS_CONTACT "info@nano.org")
		set (CPACK_NSIS_MENU_LINKS "nano_wallet.exe" "Nano Wallet" "https://nano.org" "Nano website")
		set (CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
		get_target_property (Qt5WindowsPlugin Qt5::QWindowsIntegrationPlugin LOCATION)
		get_filename_component (Qt5_bin_DIR ${Qt5_DIR}/../../../bin ABSOLUTE)
		install (TARGETS nano_wallet DESTINATION .)
		install (TARGETS nano_wallet_com DESTINATION .)
		install (FILES ${CMAKE_CURRENT_BINARY_DIR}/${WIN_REDIST} DESTINATION .)
		install (FILES ${Qt5_bin_DIR}/libGLESv2.dll DESTINATION .)
		install (FILES ${Qt5_bin_DIR}/Qt5Core.dll DESTINATION .)
		install (FILES ${Qt5_bin_DIR}/Qt5Gui.dll DESTINATION .)
		install (FILES ${Qt5_bin_DIR}/Qt5Widgets.dll DESTINATION .)
		install (FILES ${Qt5_bin_DIR}/Qt5WinExtras.dll DESTINATION .)
		install (FILES ${Qt5WindowsPlugin} DESTINATION platforms)
	else ()
		set(CPACK_GENERATOR "TBZ2")
		install(TARGETS nano_wallet
				RUNTIME DESTINATION .
		)
	endif ()
endif ()

set (CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)

include (CPack)
