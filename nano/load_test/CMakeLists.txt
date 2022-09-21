add_executable(load_test entry.cpp)

target_link_libraries(
  load_test
  node
  secure
  test_common
  gtest
  Boost::boost
  Boost::coroutine
  Boost::context)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
include_directories(${CMAKE_SOURCE_DIR}/submodules/cpptoml/include)
include_directories(${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include)
