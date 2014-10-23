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

add_library (secure
	rai/secure.hpp
	rai/secure.cpp
	)

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
    rai/test/block_store.cpp
    rai/test/test_network.cpp
    rai/test/wallet_test.cpp
    rai/test/daemon.cpp
    rai/test/uint256_union.cpp
    rai/test/bulk_req_initiator.cpp
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

SET (GENESIS_KEY rai_test_address CACHE STRING "Select whether the genesis address is the test or live address")
set_property (CACHE GENESIS_KEY PROPERTY STRINGS rai_test_address rai_live_address)

SET (WORK_FACTOR rai_test_work CACHE STRING "Selects whether full or test work is performed")
set_property (CACHE WORK_FACTOR PROPERTY STRINGS rai_test_work rai_full_work)

set_target_properties (secure core test qt_test cli qt qt_client qt_system PROPERTIES COMPILE_FLAGS "-fPIC -std=c++11 -Werror=switch ${PLATFORM_DEFINES} -DQT_NO_KEYWORDS -DGENESIS_KEY=${GENESIS_KEY} -DWORK_FACTOR=${WORK_FACTOR} -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")

if (WIN32)
    set (PLATFORM_LIBS ws2_32 mswsock)
else (WIN32)
    set (PLATFORM_LIBS)
endif (WIN32)

target_link_libraries (test core secure ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (cli core secure ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_test core secure qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_client core secure qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (qt_system core secure qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_MAIN_LIBRARY} ${GTEST_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})
