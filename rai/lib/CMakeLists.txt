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

add_library (rai_lib
	${platform_sources}
	errors.hpp
	errors.cpp
	expected.hpp
	blocks.cpp
	blocks.hpp
	config.hpp
	interface.cpp
	interface.h
	numbers.cpp
	numbers.hpp
	utility.cpp
	utility.hpp
	work.hpp
	work.cpp)

target_link_libraries (rai_lib
	xxhash
	blake2
	${CRYPTOPP_LIBRARY}
	Boost::boost)

target_compile_definitions(rai_lib
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
)

if (RAIBLOCKS_GUI AND NOT APPLE)
	install(TARGETS rai_lib
			RUNTIME DESTINATION bin
			LIBRARY DESTINATION lib
			ARCHIVE DESTINATION lib
	)
endif()
