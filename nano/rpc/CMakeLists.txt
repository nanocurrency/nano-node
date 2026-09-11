add_library(
  rpc
  rpc_server.hpp
  rpc_server.cpp
  rpc_connection.hpp
  rpc_connection.cpp
  rpc_dispatcher.hpp
  rpc_dispatcher.cpp
  rpc_request_processor.hpp
  rpc_request_processor.cpp)

target_link_libraries(rpc nano_lib Boost::beast)
