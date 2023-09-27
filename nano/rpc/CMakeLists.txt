if(NANO_SECURE_RPC OR RAIBLOCKS_SECURE_RPC)
  set(secure_rpc_sources rpc_secure.hpp rpc_secure.cpp
                         rpc_connection_secure.hpp rpc_connection_secure.cpp)
endif()

add_library(
  rpc
  ${secure_rpc_sources}
  rpc.hpp
  rpc.cpp
  rpc_connection.hpp
  rpc_connection.cpp
  rpc_handler.hpp
  rpc_handler.cpp
  rpc_request_processor.hpp
  rpc_request_processor.cpp)

target_link_libraries(rpc nano_lib Boost::beast ${OPENSSL_LIBRARIES})
