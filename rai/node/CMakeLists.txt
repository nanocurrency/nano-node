if (RAIBLOCKS_SECURE_RPC)
	set (secure_rpc_sources rpc_secure.cpp rpc_secure.hpp)
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	# No opencl
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (platform_sources plat/windows/openclapi.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (platform_sources plat/posix/openclapi.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (platform_sources plat/posix/openclapi.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()

add_library (node
	${platform_sources}
	${secure_rpc_sources}
	bootstrap.cpp
	bootstrap.hpp
	cli.hpp
	cli.cpp
	common.cpp
	lmdb.cpp
	lmdb.hpp
	curve25519_dalek.hpp
	node.hpp
	node.cpp
	openclwork.cpp
	openclwork.hpp
	rpc.hpp
	rpc.cpp
	testing.hpp
	testing.cpp
	wallet.hpp
	wallet.cpp
	stats.hpp
	stats.cpp
	working.hpp
	xorshift.hpp)

include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

ExternalProject_Add(
	curve25519_dalek_c
	DOWNLOAD_COMMAND ""
	CONFIGURE_COMMAND ""
	BUILD_COMMAND cargo build COMMAND cargo build --release
	BINARY_DIR "${CMAKE_SOURCE_DIR}/curve25519_dalek_c"
	INSTALL_COMMAND "")

ExternalProject_Add_Step(curve25519_dalek_c forcebuild
    COMMAND ${CMAKE_COMMAND} -E echo "Force build of curve25519_dalek_c"
    DEPENDEES update
    DEPENDERS configure
    ALWAYS 1)

add_dependencies (node curve25519_dalek_c)

if (WIN32)
	set (CURVE25519_DALEK_LIB_NAME curve25519_dalek_c.lib)
	set (RUST_DEPS userenv)
else ()
	set (CURVE25519_DALEK_LIB_NAME libcurve25519_dalek_c.a)
	set (RUST_DEPS)
endif ()

target_link_libraries (node
	secure
	rai_lib
	libminiupnpc-static
	argon2
	lmdb
	Boost::filesystem
	Boost::log
	Boost::log_setup
	Boost::program_options
	Boost::system
	Boost::thread
	Boost::boost
	debug "${CMAKE_SOURCE_DIR}/curve25519_dalek_c/target/debug/${CURVE25519_DALEK_LIB_NAME}"
	optimized "${CMAKE_SOURCE_DIR}/curve25519_dalek_c/target/release/${CURVE25519_DALEK_LIB_NAME}"
	${RUST_DEPS}
	${CMAKE_DL_LIBS}
	)

target_compile_definitions(node
	PRIVATE
		-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR})
