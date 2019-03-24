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
	blockprocessor.hpp
	blockprocessor.cpp
	bootstrap.hpp
	bootstrap.cpp
	cli.hpp
	cli.cpp
	common.hpp
	common.cpp
	daemonconfig.hpp
	daemonconfig.cpp
	ipc.hpp
	ipc.cpp
	ipcconfig.hpp
	ipcconfig.cpp
	lmdb.hpp
	lmdb.cpp
	logging.hpp
	logging.cpp
	nodeconfig.hpp
	nodeconfig.cpp
	node.hpp
	node.cpp
	openclconfig.hpp
	openclconfig.cpp
	openclwork.hpp
	openclwork.cpp
	portmapping.hpp
	portmapping.cpp
	repcrawler.hpp
	repcrawler.cpp
	testing.hpp
	testing.cpp
	transport/tcp.hpp
	transport/tcp.cpp
	transport/transport.hpp
	transport/transport.cpp
	transport/udp.hpp
	transport/udp.cpp
	signatures.hpp
	signatures.cpp
	stats.hpp
	stats.cpp
	voting.hpp
	voting.cpp
	wallet.hpp
	wallet.cpp
	websocket.hpp
	websocket.cpp
	websocketconfig.hpp
	websocketconfig.cpp
	working.hpp
	xorshift.hpp)

target_link_libraries (node
	secure
	nano_lib
	libminiupnpc-static
	argon2
	lmdb
	crypto_lib
	Boost::filesystem
	Boost::log
	Boost::log_setup
	Boost::program_options
	Boost::system
	Boost::thread
	Boost::boost
	${CMAKE_DL_LIBS})

target_compile_definitions(node
	PRIVATE
		-DNANO_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DNANO_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DNANO_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})
