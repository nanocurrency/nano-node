add_library (rpc
	rpc.cpp
	rpc.hpp
	rpc_connection.cpp
	rpc_connection.hpp
	rpc_handler.cpp
	rpc_handler.hpp)

target_link_libraries (rpc
	nano_lib)
