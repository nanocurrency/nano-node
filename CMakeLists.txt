cmake_minimum_required (VERSION 2.8.11)
project (rai)

if (WIN32)
    set (PLATFORM_DEFINES "-D_WIN32_WINNT=0x0501 -DWINVER=0x0501")
else (WIN32)
    set (PLATFORM_DEFINES)
endif (WIN32)

include_directories (${CMAKE_SOURCE_DIR})

find_package (Boost REQUIRED COMPONENTS filesystem system thread program_options)
include_directories (${Boost_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

find_package (CryptoPP REQUIRED)
include_directories (${CRYPTOPP_INCLUDE_DIR})

find_package (Qt5 REQUIRED COMPONENTS Core Gui Widgets Test)
include_directories (${Qt5Core_INCLUDE_DIRS} ${Qt5Gui_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS} ${Qt5Test_INCLUDE_DIRS})

find_package (cppnetlib 0.11.0 REQUIRED)
include_directories (${CPPNETLIB_INCLUDE_DIRS})

find_package (LevelDB REQUIRED)
include_directories (${LevelDB_INCLUDE_PATH})

add_library (core
	rai/core/core.hpp
	rai/core/core.cpp
    ed25519-donna/ed25519-hash-custom.h
    ed25519-donna/ed25519-randombytes-custom.h
    ed25519-donna/ed25519.h
    ed25519-donna/ed25519.cpp
    )

add_executable (test
    rai/test/client.cpp
	rai/test/entry.cpp
	rai/test/ledger.cpp
	rai/test/block.cpp
    rai/test/db_test.cpp
    rai/test/test_network.cpp
    rai/test/wallet_test.cpp
    rai/test/daemon.cpp
    )
    
add_executable (qt_test
    rai/qt_test/qt.cpp)
    
add_executable (cli
    rai/cli/daemon.cpp
    rai/cli/daemon.hpp
    rai/cli/entry.cpp)
    
add_library (qt
    rai/qt/qt.cpp
    rai/qt/qt.hpp)
    
add_executable (qt_client
    rai/qt_client/entry.cpp)
    
add_executable (qt_system
    rai/qt_system/entry.cpp)

set_target_properties (core test qt_test cli qt qt_client qt_system PROPERTIES COMPILE_FLAGS "-fPIC -std=c++11 -Werror=switch ${PLATFORM_DEFINES} -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")

if (WIN32)
    set (PLATFORM_LIBS ws2_32 mswsock)
else (WIN32)
    set (PLATFORM_LIBS)
endif (WIN32)

target_link_libraries (test core ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (cli core ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_test core qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_client core qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_system core qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})