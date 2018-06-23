if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set (PLATFORM_SECURE_SOURCE plat/osx/working.mm)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (PLATFORM_SECURE_SOURCE plat/windows/working.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (PLATFORM_SECURE_SOURCE plat/posix/working.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (PLATFORM_SECURE_SOURCE plat/posix/working.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()

# Embed bootstrap representative weights in executable
file (READ ${CMAKE_SOURCE_DIR}/rep_weights.bin filedata HEX)
string (REGEX REPLACE "(..)" "0x\\1," filedata ${filedata})
file (WRITE ${CMAKE_BINARY_DIR}/bootstrap_weights.cpp "#include <cstddef>\n"
	"namespace rai {\n"
	"	unsigned char rai_bootstrap_weights[] = {${filedata} 0x00};\n"
	"	size_t rai_bootstrap_weights_size = sizeof(rai_bootstrap_weights) - 1;\n"
	"}\n")

add_library (secure
	${PLATFORM_SECURE_SOURCE}
	${CMAKE_BINARY_DIR}/bootstrap_weights.cpp
	config.hpp
	common.cpp
	common.hpp
	blockstore.cpp
	blockstore.hpp
	ledger.cpp
	ledger.hpp
	utility.cpp
	utility.hpp
	versioning.hpp
	versioning.cpp)

target_link_libraries(secure rai_lib Boost::boost)

target_compile_definitions(secure PUBLIC
	-DQT_NO_KEYWORDS -DACTIVE_NETWORK=${ACTIVE_NETWORK}
	-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
	-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
	-DBOOST_ASIO_HAS_STD_ARRAY=1)
# TODO: Do globally
set_target_properties (secure PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")
