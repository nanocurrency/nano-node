add_executable(ipc_flatbuffers_test_client entry.cpp)

target_link_libraries(
  ipc_flatbuffers_test_client
  nano_lib
  Boost::filesystem
  Boost::log_setup
  Boost::log
  Boost::program_options
  Boost::system
  Boost::thread)
