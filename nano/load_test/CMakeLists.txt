add_executable(load_test entry.cpp)

#target_link_libraries(load_test test_common Boost::process)
target_link_libraries(load_test test_common Boost::boost)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
include_directories(${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include)
