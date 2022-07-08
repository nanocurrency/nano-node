add_library(
  test_common
  ledger.hpp
  ledger.cpp
  network.hpp
  network.cpp
  system.hpp
  system.cpp
  telemetry.hpp
  telemetry.cpp
  testutil.hpp
  testutil.cpp)

target_link_libraries(test_common node gtest Boost::log_setup Boost::log
                      Boost::boost)
