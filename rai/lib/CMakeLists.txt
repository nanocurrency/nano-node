if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set (platform_sources plat/default/priority.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (platform_sources plat/windows/priority.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (platform_sources plat/linux/priority.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (platform_sources plat/default/priority.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()


SET (sources
	${platform_sources}
	errors.hpp
	errors.cpp
	expected.hpp
	blocks.cpp
	blocks.hpp
	interface.cpp
	interface.h
	numbers.cpp
	numbers.hpp
	utility.cpp
	utility.hpp
	work.hpp
	work.cpp)

add_library (rai_lib SHARED ${sources})
add_library (rai_lib_static STATIC ${sources})

target_link_libraries (rai_lib
	secure
	ed25519
	xxhash
	blake2
	${CRYPTOPP_LIBRARY}
	Boost::boost)

target_link_libraries (rai_lib_static
	secure
	ed25519
	xxhash
	blake2
	${CRYPTOPP_LIBRARY}
	Boost::boost)
