add_library(
  rpc
  rpc.hpp
  rpc.cpp
  rpc_connection.hpp
  rpc_connection.cpp
  rpc_handler.hpp
  rpc_handler.cpp
  rpc_request_processor.hpp
  rpc_request_processor.cpp)

#target_link_libraries(rpc nano_lib Boost::beast)
target_link_libraries(rpc nano_lib Boost::boost)
