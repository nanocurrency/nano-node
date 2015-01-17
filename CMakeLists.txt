cmake_minimum_required (VERSION 2.8.11)
project (rai)

if (WIN32)
	set (PLATFORM_COMPILE_FLAGS "-D_WIN32_WINNT=0x0501 -DWINVER=0x0501")
else (WIN32)
	set (PLATFORM_COMPILE_FLAGS -fPIC)
endif (WIN32)

if (WIN32)
	set (PLATFORM_GUI_EXE_LINK_FLAGS "-mwindows")
else (WIN32)
	set (PLATFORM_GUI_EXE_LINK_FLAGS "")
endif (WIN32)

if (APPLE)
	set (PLATFORM_LINK_FLAGS "-framework Foundation")
else (APPLE)
	set (PLATFORM_LINK_FLAGS "")
endif (APPLE)

include_directories (${CMAKE_SOURCE_DIR})

find_package (Boost REQUIRED COMPONENTS filesystem system log log_setup thread program_options)
include_directories (${Boost_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

find_package (CryptoPP REQUIRED)
include_directories (${CRYPTOPP_INCLUDE_DIR})

find_package (Qt5 REQUIRED COMPONENTS Core Gui Widgets Test)
include_directories (${Qt5Core_INCLUDE_DIRS} ${Qt5Gui_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS} ${Qt5Test_INCLUDE_DIRS})

include_directories (${CPPNETLIB_INCLUDE_DIRS})

find_package (LevelDB REQUIRED)
include_directories (${LevelDB_INCLUDE_PATH})

add_library (ed25519
	ed25519-donna/ed25519-hash-custom.h
	ed25519-donna/ed25519-randombytes-custom.h
	ed25519-donna/ed25519.h
	ed25519-donna/ed25519.c)

add_library (secure
	rai/secure.hpp
	rai/secure.cpp)

if (APPLE)
	set (PLATFORM_NODE_SOURCE rai/plat/osx/working.mm)
elseif (WIN32)
	set (PLATFORM_NODE_SOURCE rai/plat/windows/working.cpp)
elseif (UNIX)
	set (PLATFORM_NODE_SOURCE rai/plat/posix/working.cpp)
endif (APPLE)

add_library (node
	rai/working.hpp
	${PLATFORM_NODE_SOURCE}
	rai/node.hpp
	rai/node.cpp)

add_executable (core_test
	rai/core_test/block.cpp
	rai/core_test/block_store.cpp
	rai/core_test/block_synchronization.cpp
	rai/core_test/node.cpp
	rai/core_test/conflicts.cpp
	rai/core_test/daemon.cpp
	rai/core_test/entry.cpp
	rai/core_test/gap_cache.cpp
	rai/core_test/ledger.cpp
	rai/core_test/message.cpp
	rai/core_test/message_parser.cpp
	rai/core_test/processor_service.cpp
	rai/core_test/peer_container.cpp
	rai/core_test/rpc.cpp
	rai/core_test/network.cpp
	rai/core_test/uint256_union.cpp
	rai/core_test/wallet.cpp
	rai/core_test/wallets.cpp
	rai/core_test/work_store.cpp)

add_executable (slow_test
	rai/slow_test/node.cpp)
    
add_executable (qt_test
	rai/qt_test/qt.cpp)
    
add_executable (cli
	rai/cli/daemon.cpp
	rai/cli/daemon.hpp
	rai/cli/entry.cpp)
    
add_library (qt
	rai/qt/qt.cpp
	rai/qt/qt.hpp)

add_executable (landing
	rai/landing/entry.cpp)
    
add_executable (qt_wallet
	rai/qt_wallet/entry.cpp)
    
add_executable (qt_system
	rai/qt_system/entry.cpp)

set (CPACK_PACKAGE_VERSION_MAJOR "4")
set (CPACK_PACKAGE_VERSION_MINOR "0")
set (CPACK_PACKAGE_VERSION_PATCH "1")

SET (ACTIVE_NETWORK rai_active_network CACHE STRING "Selects which network parameters are used")
set_property (CACHE ACTIVE_NETWORK PROPERTY STRINGS rai_test_network rai_beta_network rai_live_network)

set_target_properties (ed25519 PROPERTIES COMPILE_FLAGS "-DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")
set_target_properties (secure node core_test slow_test qt_test cli qt qt_wallet qt_system landing PROPERTIES COMPILE_FLAGS "-std=c++11 -Werror=switch ${PLATFORM_COMPILE_FLAGS} -DQT_NO_KEYWORDS -DACTIVE_NETWORK=${ACTIVE_NETWORK} -DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR} -DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR} -DRAIBLOCKS_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH}")
set_target_properties (secure node core_test slow_test cli qt qt_system landing PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")
set_target_properties (qt_wallet qt_test PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS} ${PLATFORM_GUI_EXE_LINK_FLAGS}")

if (WIN32)
	set (PLATFORM_LIBS ws2_32 mswsock)
else (WIN32)
	set (PLATFORM_LIBS)
endif (WIN32)

target_link_libraries (core_test node secure ed25519 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (slow_test node secure ed25519 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (cli node secure ed25519 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (landing node secure ed25519 ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_test node secure ed25519 qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_wallet node secure ed25519 qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_system node secure ed25519 qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

if (APPLE)
	get_filename_component (Qt5Core_framework_DIR ${Qt5_DIR}/../../QtCore.framework ABSOLUTE)
	message ("Qt5Core ${Qt5Core_framework_DIR}")
	get_filename_component (Qt5Gui_framework_DIR ${Qt5_DIR}/../../QtGui.framework ABSOLUTE)
	get_filename_component (Qt5PrintSupport_framework_DIR ${Qt5_DIR}/../../QtPrintSupport.framework ABSOLUTE)
	get_filename_component (Qt5Test_framework_DIR ${Qt5_DIR}/../../QtTest.framework ABSOLUTE)
	get_filename_component (Qt5Widgets_framework_DIR ${Qt5_DIR}/../../QtWidgets.framework ABSOLUTE)
	get_filename_component (Qt5_platforms_DIR ${Qt5_DIR}/../../../plugins/platforms ABSOLUTE)

	file (COPY ${Qt5Core_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtCore.framework/Versions/5/QtCore")

	file (COPY ${Qt5Gui_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtGui.framework/Versions/5/QtGui")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtGui.framework/Versions/5/QtGui")

	file (COPY ${Qt5PrintSupport_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtPrintSupport.framework/Versions/5/QtPrintSupport" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Widgets_framework_DIR}/Versions/5/QtWidgets" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework/Versions/5/QtPrintSupport")

	file (COPY ${Qt5Test_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtTest.framework/Versions/5/QtTest" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtTest.framework/Versions/5/QtTest")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtTest.framework/Versions/5/QtTest")

	file (COPY ${Qt5Widgets_framework_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-id" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework/Versions/5/QtWidgets")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework/Versions/5/QtWidgets")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework/Versions/5/QtWidgets")

	file (COPY ${Qt5_DIR}/../../../plugins/platforms/libqcocoa.dylib DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5PrintSupport_framework_DIR}/Versions/5/QtPrintSupport" "@executable_path/../Frameworks/QtPrintSupport.framework/Versions/5/QtPrintSupport" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Widgets_framework_DIR}/Versions/5/QtWidgets" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib")

	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:qt_wallet> ${CMAKE_CURRENT_BINARY_DIR}/packaging)
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Core_framework_DIR}/Versions/5/QtCore" "@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore" "${CMAKE_CURRENT_BINARY_DIR}/packaging/qt_wallet")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Gui_framework_DIR}/Versions/5/QtGui" "@executable_path/../Frameworks/QtGui.framework/Versions/5/QtGui" "${CMAKE_CURRENT_BINARY_DIR}/packaging/qt_wallet")
	add_custom_command (TARGET qt_wallet POST_BUILD COMMAND "install_name_tool" "-change" "${Qt5Widgets_framework_DIR}/Versions/5/QtWidgets" "@executable_path/../Frameworks/QtWidgets.framework/Versions/5/QtWidgets" "${CMAKE_CURRENT_BINARY_DIR}/packaging/qt_wallet")

	install (FILES ${CMAKE_CURRENT_BINARY_DIR}/packaging/qt_wallet DESTINATION RaiBlocks.app/Contents/MacOS PERMISSIONS OWNER_EXECUTE OWNER_READ)
	install (FILES Info.plist DESTINATION RaiBlocks.app/Contents)
	install (FILES qt.conf DESTINATION RaiBlocks.app/Contents/Resources)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtCore.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtGui.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtPrintSupport.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtTest.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packaging/QtWidgets.framework" DESTINATION RaiBlocks.app/Contents/Frameworks USE_SOURCE_PERMISSIONS)
	install (FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging/libqcocoa.dylib" DESTINATION RaiBlocks.app/Contents/PlugIns/platforms PERMISSIONS OWNER_EXECUTE OWNER_READ)
elseif (WIN32)
	get_filename_component (Qt5_bin_DIR ${Qt5_DIR}/../../../bin ABSOLUTE)
	install (TARGETS qt_wallet DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/icudt52.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/icuin52.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/icuuc52.dll DESTINATION .)
	install (FILES c:/MinGW/bin/libgcc_s_dw2-1.dll DESTINATION .)
	install (FILES c:/MinGW/bin/libstdc++-6.dll DESTINATION .)
	install (FILES c:/MinGW/bin/libwinpthread-1.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5Core.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5Gui.dll DESTINATION .)
	install (FILES ${Qt5_bin_DIR}/Qt5Widgets.dll DESTINATION .)
	install (FILES ${Qt5_DIR}/../../../plugins/platforms/qwindows.dll DESTINATION platforms)
else ()
	install (TARGETS qt_wallet DESTINATION .)
endif ()

include (CPack)
