add_executable(rpc_test entry.cpp rpc.cpp)

target_link_libraries(rpc_test node secure rpc test_common gtest)

target_compile_definitions(
  rpc_test
  PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK}
  PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
          -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

include_directories(${CMAKE_SOURCE_DIR}/submodules)
include_directories(${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include)
