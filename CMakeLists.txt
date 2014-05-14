cmake_minimum_required (VERSION 2.8)
project (mu_coin)

add_definitions (-std=c++11)

include_directories (${CMAKE_SOURCE_DIR})

find_package (BerkeleyDB REQUIRED)
include_directories (${BerkeleyDB_INCLUDE_DIR})

find_package (Boost REQUIRED COMPONENTS filesystem system)
include_directories (${Boost_INCLUDE_DIR})

find_package (GTest REQUIRED)
include_directories (${GTEST_INCLUDE_DIR})

find_package (CryptoPP REQUIRED)
include_directories (${CRYPTOPP_INCLUDE_DIR})

add_library (mu_coin
	mu_coin/mu_coin.hpp
	mu_coin/mu_coin.cpp)
    
add_library (mu_coin_store
    mu_coin_store/db.hpp
    mu_coin_store/db.cpp)

add_executable (mu_coin_test 
	mu_coin_test/entry.cpp
	mu_coin_test/ledger.cpp
	mu_coin_test/block.cpp
    mu_coin_test/db.cpp)

target_link_libraries (mu_coin_test mu_coin_store ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${BerkeleyDB_LIBRARIES} mu_coin)