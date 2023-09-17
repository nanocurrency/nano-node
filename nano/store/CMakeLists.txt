add_library(nano_store component.cpp component.hpp)

target_link_libraries(
  nano_store
  Boost::circular_buffer
  Boost::endian
  Boost::filesystem
  Boost::iostreams
  Boost::log_setup
  Boost::log
  Boost::multiprecision
  Boost::pool
  Boost::stacktrace
  Boost::variant)
