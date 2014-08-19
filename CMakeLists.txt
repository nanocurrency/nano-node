cmake_minimum_required (VERSION 2.8.11)
project (mu_coin)

add_definitions (-std=c++11 -Werror=switch -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG)

include_directories (${CMAKE_SOURCE_DIR})

find_package (BerkeleyDB REQUIRED)
include_directories (${BerkeleyDB_INCLUDE_DIR})

find_package (Boost REQUIRED COMPONENTS filesystem system thread)
include_directories (${Boost_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

find_package (CryptoPP REQUIRED)
include_directories (${CRYPTOPP_INCLUDE_DIR})

find_package (Qt5 REQUIRED COMPONENTS Core Gui Widgets)

find_package (cppnetlib 0.11.0 REQUIRED)
include_directories (${CPPNETLIB_INCLUDE_DIRS})

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
    
add_executable (mu_coin_qt
    mu_coin_qt/entry.cpp
    mu_coin_qt/qt.cpp
    mu_coin_qt/qt.hpp)

target_link_libraries (mu_coin_test mu_coin ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${BerkeleyDB_LIBRARIES} ${CPPNETLIB_LIBRARIES} pthread)

target_link_libraries (mu_coin_qt mu_coin ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${BerkeleyDB_LIBRARIES} ${CPPNETLIB_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets ${QT_QTGUI_LIBRARY} pthread)