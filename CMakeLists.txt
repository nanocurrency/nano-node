cmake_minimum_required (VERSION 2.8)
project (mu_coin)

include_directories (${CMAKE_SOURCE_DIR})

find_package (Boost REQUIRED)
include_directories (${Boost_INCLUDE_DIR})

find_package (OpenSSL REQUIRED)
include_directories (${OpenSSL_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

add_library (mu_coin 
	mu_coin/entry.cpp
	mu_coin/address.hpp
	mu_coin/address.cpp
	mu_coin/block.hpp
	mu_coin/block.cpp
	mu_coin/ledger.hpp
	mu_coin/ledger.cpp
	mu_coin/balance.hpp
	mu_coin/balance.cpp)

add_executable (mu_coin_test 
	mu_coin_test/entry.cpp
	mu_coin_test/ledger.cpp
    mu_coin_test/balance.cpp
    mu_coin_test/block.cpp)

target_link_libraries (mu_coin_test ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} mu_coin)