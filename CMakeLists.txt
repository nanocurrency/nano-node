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

find_package (Qt4 REQUIRED)
include_directories (/opt/local/include)
include_directories (${QT_QTCORE_INCLUDE_DIR})
include_directories (${QT_QTGUI_INCLUDE_DIR})

find_package (Protobuf REQUIRED)
include_directories (${PROTOBUF_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP (PROTO_SRCS PROTO_HDRS mu_coin_network/messages.proto)

add_library (mu_coin
	mu_coin/mu_coin.hpp
	mu_coin/mu_coin.cpp)
    
add_library (mu_coin_store
    mu_coin_store/db.hpp
    mu_coin_store/db.cpp)
    
add_library (mu_coin_network
    mu_coin_network/messages.proto
    mu_coin_network/messages.hpp
    mu_coin_network/messages.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS})

add_executable (mu_coin_test 
	mu_coin_test/entry.cpp
	mu_coin_test/ledger.cpp
	mu_coin_test/block.cpp
    mu_coin_test/db_test.cpp
    mu_coin_test/messages_test.cpp)
    
add_executable (mu_coin_client
    mu_coin_client/entry.cpp)

target_link_libraries (mu_coin_test mu_coin_store mu_coin_network ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${BerkeleyDB_LIBRARIES} ${PROTOBUF_LIBRARY} mu_coin)

target_link_libraries (mu_coin_client mu_coin_store mu_coin_network ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} ${CRYPTOPP_LIBRARY} ${BerkeleyDB_LIBRARIES} ${PROTOBUF_LIBRARY} ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY} mu_coin)