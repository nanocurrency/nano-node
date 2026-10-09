add_executable(qt_test entry.cpp qt.cpp)

target_link_libraries(
  qt_test
  node
  secure
  test_common
  gtest
  gtest_main
  qt
  Qt5::Test)

set_target_properties(
  qt_test PROPERTIES COMPILE_FLAGS
                     "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_dependencies(all_tests qt_test)
