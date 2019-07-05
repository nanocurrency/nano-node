if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set (platform_sources plat/default/priority.cpp plat/posix/perms.cpp plat/darwin/thread_role.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (platform_sources plat/windows/priority.cpp plat/windows/perms.cpp plat/windows/registry.cpp plat/windows/thread_role.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (platform_sources plat/linux/priority.cpp plat/posix/perms.cpp plat/linux/thread_role.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (platform_sources plat/default/priority.cpp plat/posix/perms.cpp plat/freebsd/thread_role.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()

add_library (nano_lib
	${platform_sources}
	blockbuilders.hpp
	blockbuilders.cpp
	blocks.hpp
	blocks.cpp
	config.hpp
	config.cpp
	errors.hpp
	errors.cpp
	interface.h
	interface.cpp
	ipc.hpp
	ipc.cpp
	ipc_client.hpp
	ipc_client.cpp
	json_error_response.hpp
	jsonconfig.hpp
	logger_mt.hpp
	memory.hpp
	memory.cpp
	numbers.hpp
	numbers.cpp
	rpc_handler_interface.hpp
	rpcconfig.hpp
	rpcconfig.cpp
	stats.hpp
	stats.cpp
	timer.hpp
	utility.hpp
	utility.cpp
	work.hpp
	work.cpp)

target_link_libraries (nano_lib
	ed25519
	crypto_lib
	blake2
	${CRYPTOPP_LIBRARY}
	Boost::boost)

target_compile_definitions(nano_lib
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
)
