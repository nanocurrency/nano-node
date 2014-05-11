cmake_minimum_required (VERSION 2.8)
project (mu_coin)

include_directories (${CMAKE_SOURCE_DIR})

find_package (Boost REQUIRED)
include_directories (${Boost_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

find_package (CryptoPP REQUIRED)
include_directories (${CRYPTOPP_INCLUDE_DIRS})

add_library (mu_coin 
	mu_coin/entry.cpp
	mu_coin/mu_coin.hpp
	mu_coin/mu_coin.cpp
	mu_coin/types.hpp)

add_executable (mu_coin_test 
	mu_coin_test/entry.cpp
	mu_coin_test/ledger.cpp
	mu_coin_test/block.cpp)

target_link_libraries (mu_coin_test ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} mu_coin)