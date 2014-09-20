cmake_minimum_required (VERSION 2.8.11)
project (mu_coin)

if (WIN32)
    set (PLATFORM_DEFINES -D_WIN32_WINNT=0x0501 -DWINVER=0x0501)
else (WIN32)
    set (PLATFORM_DEFINES)
endif (WIN32)

include_directories (${CMAKE_SOURCE_DIR})

find_package (Boost REQUIRED COMPONENTS filesystem system thread)
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

add_library (mu_coin
	mu_coin/mu_coin.hpp
	mu_coin/mu_coin.cpp
    ed25519-donna/ed25519-hash-custom.h
    ed25519-donna/ed25519-randombytes-custom.h
    ed25519-donna/ed25519.h
    ed25519-donna/ed25519.cpp)

add_executable (mu_coin_test 
	mu_coin_test/entry.cpp
	mu_coin_test/ledger.cpp
	mu_coin_test/block.cpp
    mu_coin_test/db_test.cpp
    mu_coin_test/test_network.cpp
    mu_coin_test/wallet_test.cpp)
    
add_executable (mu_coin_qt_test
    mu_coin_qt_test/qt.cpp)
    
add_executable (mu_coin_cli
    mu_coin_cli/entry.cpp)
    
add_library (mu_coin_qt
    mu_coin_qt/qt.cpp
    mu_coin_qt/qt.hpp)
    
add_executable (mu_coin_qt_client
    mu_coin_qt_client/entry.cpp)
    
add_executable (mu_coin_qt_system
    mu_coin_qt_system/entry.cpp)

set_target_properties (mu_coin mu_coin_test mu_coin_qt_test mu_coin_cli mu_coin_qt mu_coin_qt_client mu_coin_qt_system PROPERTIES COMPILE_FLAGS "-std=c++11 ${PLATFORM_DEFINES} -Werror=switch -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")
    
if (WIN32)
    set (PLATFORM_LIBS ws2_32 mswsock)
else (WIN32)
    set (PLATFORM_LIBS)
endif (WIN32)

target_link_libraries (mu_coin_test mu_coin ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (mu_coin_cli mu_coin ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (mu_coin_qt_test mu_coin mu_coin_qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (mu_coin_qt_client mu_coin mu_coin_qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})

target_link_libraries (mu_coin_qt_system mu_coin mu_coin_qt ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread ${PLATFORM_LIBS} ${LevelDB_LIBRARY})