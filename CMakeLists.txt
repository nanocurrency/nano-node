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
	rai/test/block.cpp
    rai/test/block_store.cpp
    rai/test/client.cpp
    rai/test/daemon.cpp
	rai/test/entry.cpp
	rai/test/ledger.cpp
    rai/test/message.cpp
    rai/test/processor_service.cpp
    rai/test/peer_container.cpp
    rai/test/network.cpp
    rai/test/uint256_union.cpp
    rai/test/wallet_test.cpp
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

SET (KDF_WORK_FACTOR kdf_test_work CACHE STRING "Selects whether full or test kdf work is performed")
set_property (CACHE KDF_WORK_FACTOR PROPERTY STRINGS kdf_test_work kdf_full_work)

SET (PUBLISH_WORK_FACTOR publish_test_work CACHE STRING "Selects whether full or test publish work is performed")
set_property (CACHE PUBLISH_WORK_FACTOR PROPERTY STRINGS publish_test_work publish_full_work)

set_target_properties (secure core test qt_test cli qt qt_client qt_system PROPERTIES COMPILE_FLAGS "-std=c++11 -Werror=switch ${PLATFORM_COMPILE_FLAGS} -DQT_NO_KEYWORDS -DGENESIS_KEY=${GENESIS_KEY} -DKDF_WORK_FACTOR=${KDF_WORK_FACTOR} -DPUBLISH_WORK_FACTOR=${PUBLISH_WORK_FACTOR} -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")
set_target_properties (qt_client qt_test PROPERTIES LINK_FLAGS "${PLATFORM_GUI_EXE_LINK_FLAGS}")

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
