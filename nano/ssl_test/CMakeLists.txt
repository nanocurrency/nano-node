add_executable(ssl_test entry.cpp ssl.cpp)

find_package(OpenSSL REQUIRED COMPONENTS Crypto SSL)

target_link_libraries(
  ssl_test
  test_common
  gtest
  Boost::log_setup
  Boost::log
  Boost::boost
  OpenSSL::Crypto
  OpenSSL::SSL)
