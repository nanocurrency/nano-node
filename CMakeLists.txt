cmake_minimum_required (VERSION 2.8.11)
project (rai)

if (WIN32)
	set (PLATFORM_COMPILE_FLAGS "-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DWIN32_LEAN_AND_MEAN")
else (WIN32)
	set (PLATFORM_COMPILE_FLAGS "-DBOOST_NETWORK_NO_LIB -fPIC -DBOOST_ASIO_HAS_STD_ARRAY")
endif (WIN32)

if (WIN32)
	set (PLATFORM_GUI_TARGET_PROPERTIES WIN32)
else (WIN32)
	set (PLATFORM_GUI_TARGET_PROPERTIES "")
endif (WIN32)

if (APPLE)
	set (PLATFORM_LINK_FLAGS "-framework Foundation")
else (APPLE)
	set (PLATFORM_LINK_FLAGS "")
endif (APPLE)

if (WIN32)
	set (PLATFORM_QT_PACKAGES WinExtras)
else (WIN32)
	set (PLATFORM_QT_PACKAGES)
endif (WIN32)

include_directories (${CMAKE_SOURCE_DIR})

find_package (Boost REQUIRED COMPONENTS filesystem system log log_setup thread program_options)
include_directories (${Boost_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

find_package (CryptoPP REQUIRED)
include_directories (${CRYPTOPP_INCLUDE_DIR})

find_package (Qt5 REQUIRED COMPONENTS Core Gui Widgets Test ${PLATFORM_QT_PACKAGES})
include_directories (${Qt5Core_INCLUDE_DIRS} ${Qt5Gui_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS} ${Qt5Test_INCLUDE_DIRS})

include_directories (${CPPNETLIB_INCLUDE_DIRS})

add_library (ed25519
	ed25519-donna/ed25519-donna-portable.h
	ed25519-donna/ed25519-hash-custom.h
	ed25519-donna/ed25519-randombytes-custom.h
	ed25519-donna/ed25519.h
	ed25519-donna/ed25519.c)

include_directories("${CMAKE_SOURCE_DIR}/Argon2/Source/Argon2")
include_directories("${CMAKE_SOURCE_DIR}/Argon2/Source/Blake2")
include_directories("${CMAKE_SOURCE_DIR}/Argon2/Source/Core")
include_directories("${CMAKE_SOURCE_DIR}/blake2")

add_library (argon2
	Argon2/Source/Argon2/argon2.cpp
	Argon2/Source/Argon2/argon2.h
	Argon2/Source/Common/brg-endian.h
	Argon2/Source/Core/argon2-core.cpp
	Argon2/Source/Core/argon2-core.h
	Argon2/Source/Core/argon2-opt-core.cpp)

add_library (xxhash
	xxhash/xxhash.c
	xxhash/xxhash.h)

add_library (lmdb
	liblmdb/lmdb.h
	liblmdb/mdb.c
	liblmdb/midl.c)

add_library (blake2
	blake2/blake2-config.h
	blake2/blake2-impl.h
	blake2/blake2.h
	blake2/blake2b.c)

if (APPLE)
	set (PLATFORM_SECURE_SOURCE rai/plat/osx/working.mm)
elseif (WIN32)
	set (PLATFORM_SECURE_SOURCE rai/plat/windows/working.cpp)
elseif (UNIX)
	set (PLATFORM_SECURE_SOURCE rai/plat/posix/working.cpp)
endif (APPLE)

add_library (secure
	${PLATFORM_SECURE_SOURCE}
	rai/config.hpp
	rai/secure.cpp
	rai/secure.hpp
	rai/utility.cpp
	rai/utility.hpp)

add_library (node
	rai/node/node.hpp
	rai/node/node.cpp
	rai/node/rpc.hpp
	rai/node/rpc.cpp
	rai/node/testing.hpp
	rai/node/testing.cpp
	rai/node/wallet.hpp
	rai/node/wallet.cpp
	rai/node/working.hpp)

add_executable (core_test
	rai/core_test/block.cpp
	rai/core_test/block_store.cpp
	rai/core_test/block_synchronization.cpp
	rai/core_test/node.cpp
	rai/core_test/conflicts.cpp
	rai/core_test/daemon.cpp
	rai/core_test/entry.cpp
	rai/core_test/gap_cache.cpp
	rai/core_test/landing.cpp
	rai/core_test/ledger.cpp
	rai/core_test/message.cpp
	rai/core_test/message_parser.cpp
	rai/core_test/processor_service.cpp
	rai/core_test/peer_container.cpp
	rai/core_test/rpc.cpp
	rai/core_test/network.cpp
	rai/core_test/uint256_union.cpp
	rai/core_test/wallet.cpp
	rai/core_test/wallets.cpp)

add_executable (slow_test
	rai/slow_test/node.cpp)

add_executable (qt_test
	rai/qt_test/entry.cpp
	rai/qt_test/qt.cpp)

add_executable (rai_node
	rai/rai_node/daemon.cpp
	rai/rai_node/daemon.hpp
	rai/rai_node/entry.cpp)

add_library (qt
	rai/qt/qt.cpp
	rai/qt/qt.hpp)

add_executable (rai_landing
	rai/rai_landing/entry.cpp)

if (WIN32)
	set (PLATFORM_WALLET_SOURCE rai/plat/windows/icon.cpp RaiBlocks.rc)
else (WIN32)
	set (PLATFORM_WALLET_SOURCE rai/plat/unknown/icon.cpp)
endif (WIN32)

add_executable (rai_wallet ${PLATFORM_GUI_TARGET_PROPERTIES}
	${PLATFORM_WALLET_SOURCE}
	rai/rai_wallet/entry.cpp)

add_executable (qt_system
	rai/qt_system/entry.cpp)

set (CPACK_PACKAGE_VERSION_MAJOR "7")
set (CPACK_PACKAGE_VERSION_MINOR "1")
set (CPACK_PACKAGE_VERSION_PATCH "2")

SET (ACTIVE_NETWORK rai_active_network CACHE STRING "Selects which network parameters are used")
set_property (CACHE ACTIVE_NETWORK PROPERTY STRINGS rai_test_network rai_beta_network rai_live_network)

set_target_properties (argon2 PROPERTIES COMPILE_FLAGS "-msse4 -std=c++11")
set_target_properties (blake2 PROPERTIES COMPILE_FLAGS "-msse4 -std=c11")
set_target_properties (ed25519 PROPERTIES COMPILE_FLAGS "-msse4 -std=c11 -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")
set_target_properties (secure node core_test slow_test qt_test rai_node qt rai_wallet qt_system rai_landing PROPERTIES COMPILE_FLAGS "-msse4 -std=c++11 -Werror=switch ${PLATFORM_COMPILE_FLAGS} -DQT_NO_KEYWORDS -DACTIVE_NETWORK=${ACTIVE_NETWORK} -DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR} -DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR} -DRAIBLOCKS_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH}")
set_target_properties (secure node core_test slow_test rai_node qt qt_system rai_landing PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")
set_target_properties (rai_wallet qt_test PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")

if (WIN32)
	set (PLATFORM_LIBS ws2_32 mswsock)
else (WIN32)
	set (PLATFORM_LIBS)
endif (WIN32)

if (WIN32)
	set (PLATFORM_WALLET_LIBS Qt5::WinExtras)
else (WIN32)
	set (PLATFORM_WALLET_LIBS)
endif (WIN32)

target_link_libraries (core_test node secure lmdb xxhash ed25519 argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS})

target_link_libraries (slow_test node secure lmdb xxhash ed25519 argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${CRYPTOPP_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS})

target_link_libraries (rai_node node secure lmdb xxhash ed25519 argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY}  ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS})

target_link_libraries (rai_landing node secure lmdb xxhash ed25519 argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_THREAD_LIBRARY}  ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS})

target_link_libraries (qt_test node secure lmdb xxhash ed25519 qt argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS})

target_link_libraries (rai_wallet node secure lmdb xxhash ed25519 qt argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY}  ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${PLATFORM_WALLET_LIBS})

target_link_libraries (qt_system node secure lmdb xxhash ed25519 qt argon2 blake2 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS})

if (APPLE)
	get_filename_component (Qt5Core_framework_DIR ${Qt5_DIR}/../../QtCore.framework ABSOLUTE)
	get_filename_component (Qt5Gui_framework_DIR ${Qt5_DIR}/../../QtGui.framework ABSOLUTE)
	get_filename_component (Qt5PrintSupport_framework_DIR ${Qt5_DIR}/../../QtPrintSupport.framework ABSOLUTE)
	get_filename_component (Qt5Test_framework_DIR ${Qt5_DIR}/../../QtTest.framework ABSOLUTE)
	get_filename_component (Qt5Widgets_framework_DIR ${Qt5_DIR}/../../QtWidgets.framework ABSOLUTE)
	get_filename_component (Qt5_platforms_DIR ${Qt5_DIR}/../../../plugins/platforms ABSOLUTE)

	file (COPY ${Qt5Core_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtCore.framework/Versions/5/QtCore")

	file (COPY ${Qt5Gui_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtGui.framework/Versions/5/QtGui")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtGui.framework/Versions/5/QtGui")

	file (COPY ${Qt5PrintSupport_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtPrintSupport.framework/Versions/5/QtPrintSupport" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Widgets_framework_DIR}/Versions/5/QtWidgets" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")

	file (COPY ${Qt5Test_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtTest.framework/Versions/5/QtTest" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtTest.framework/Versions/5/QtTest")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtTest.framework/Versions/5/QtTest")

	file (COPY ${Qt5Widgets_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework/Versions/5/QtWidgets")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework/Versions/5/QtWidgets")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework/Versions/5/QtWidgets")

	file (COPY ${Qt5_DIR}/../../../plugins/platforms/libqcocoa.dylib DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5PrintSupport_framework_DIR}/Versions/5/QtPrintSupport" "@executable_path/../Frameworks/QtPrintSupport.framework/Versions/5/QtPrintSupport" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Widgets_framework_DIR}/Versions/5/QtWidgets" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")

	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rai_wallet> ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/rai_wallet")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/rai_wallet")
	add_custom_command (TARGET rai_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Widgets_framework_DIR}/Versions/5/QtWidgets" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/rai_wallet")

	install (FILES ${CMAKE_CURRENT_BINARY_DIR}/packaging/rai_wallet DESTINATION RaiBlocks.app/Contents/MacOS PERMISSIONS OWNER_EXECUTE OWNER_READ)
	install (FILES Info.plist DESTINATION RaiBlocks.app/Contents)
	install (FILES qt.conf DESTINATION RaiBlocks.app/Contents/Resources)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtCore.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtGui.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtTest.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib" DESTINATION RaiBlocks.app/Contents/PlugIns/platforms PERMISSIONS OWNER_EXECUTE OWNER_READ)
	install (FILES RaiBlocks.icns DESTINATION RaiBlocks.app/Contents/Resources)
elseif (WIN32)
	get_target_property (Qt5WindowsPlugin Qt5::QWindowsIntegrationPlugin LOCATION)
	get_filename_component (Qt5_bin_DIR ${Qt5_DIR}/../../../bin ABSOLUTE)
	install (TARGETS rai_wallet DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5Core.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5Gui.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5Widgets.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5WinExtras.dll DESTINATION .)
	install (FILES ${Qt5WindowsPlugin} DESTINATION platforms)
	install (FILES "../../../mingw64/bin/libwinpthread-1.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libstdc++-6.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libgcc_s_seh-1.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libicuin55.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libicuuc55.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libpcre16-0.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/zlib1.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libharfbuzz-0.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libpng16-16.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libicudt55.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libfreetype-6.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libglib-2.0-0.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libbz2-1.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libintl-8.dll" DESTINATION .)
	install (FILES "../../../mingw64/bin/libiconv-2.dll" DESTINATION .)
else ()
	install (TARGETS rai_wallet DESTINATION .)
endif ()

include (CPack)
