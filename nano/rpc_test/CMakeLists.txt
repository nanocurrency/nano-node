add_executable(rpc_test common.hpp common.cpp entry.cpp receivable.cpp rpc.cpp)

target_link_libraries(rpc_test test_common)

target_compile_definitions(
  rpc_test
  PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK}
  PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
          -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

include_directories(${CMAKE_SOURCE_DIR}/submodules)
include_directories(${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include)
