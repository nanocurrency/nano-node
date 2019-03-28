if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set (platform_sources plat/default/priority.cpp plat/posix/perms.cpp plat/darwin/thread_role.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (platform_sources plat/windows/priority.cpp plat/windows/perms.cpp plat/windows/thread_role.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (platform_sources plat/linux/priority.cpp plat/posix/perms.cpp plat/linux/thread_role.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (platform_sources plat/default/priority.cpp plat/posix/perms.cpp plat/freebsd/thread_role.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()

add_library (nano_lib
	${platform_sources}
	errors.hpp
	errors.cpp
	expected.hpp
	blockbuilders.hpp
	blockbuilders.cpp
	blocks.hpp
	blocks.cpp
	config.hpp
	config.cpp
	interface.h
	interface.cpp
	ipc.hpp
	ipc.cpp
	ipc_client.hpp
	ipc_client.cpp
	jsonconfig.hpp
	logger_mt.hpp
	rpcconfig.hpp
	rpcconfig.cpp
	numbers.hpp
	numbers.cpp
	timer.hpp
	utility.hpp
	utility.cpp
	work.hpp
	work.cpp)

target_link_libraries (nano_lib
	xxhash
	ed25519
	crypto_lib
	blake2
	${CRYPTOPP_LIBRARY}
	Boost::boost)

target_compile_definitions(nano_lib
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
)

if ((NANO_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
	install(TARGETS nano_lib
			RUNTIME DESTINATION bin
			LIBRARY DESTINATION lib
			ARCHIVE DESTINATION lib
	)
endif()
