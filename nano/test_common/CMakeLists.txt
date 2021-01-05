add_library(test_common network.hpp network.cpp telemetry.hpp telemetry.cpp
                        testutil.hpp testutil.cpp)

target_link_libraries(test_common node gtest Boost::log_setup Boost::log
                      Boost::boost)
