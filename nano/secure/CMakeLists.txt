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
if (${ACTIVE_NETWORK} MATCHES "nano_beta_network")
	set (REP_WEIGHTS_BIN_NAME "rep_weights_beta.bin")
else ()
	set (REP_WEIGHTS_BIN_NAME "rep_weights_live.bin")
endif ()

file (READ ${CMAKE_SOURCE_DIR}/${REP_WEIGHTS_BIN_NAME} filedata HEX)
string (REGEX REPLACE "(..)" "0x\\1," filedata ${filedata})
file (WRITE ${CMAKE_BINARY_DIR}/bootstrap_weights.cpp "#include <cstddef>\n"
	"namespace nano {\n"
	"	unsigned char nano_bootstrap_weights[] = {${filedata} 0x00};\n"
	"	size_t nano_bootstrap_weights_size = sizeof(nano_bootstrap_weights) - 1;\n"
	"}\n")

add_library (secure
	${PLATFORM_SECURE_SOURCE}
	${CMAKE_BINARY_DIR}/bootstrap_weights.cpp
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

target_link_libraries(secure
	ed25519
	nano_lib
	lmdb
	Boost::boost
	Boost::system
	Boost::filesystem)

target_compile_definitions(secure PUBLIC
	-DQT_NO_KEYWORDS
	-DBOOST_ASIO_HAS_STD_ARRAY=1)
