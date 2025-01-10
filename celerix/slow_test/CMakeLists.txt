add_executable(slow_test entry.cpp flamegraph.cpp node.cpp vote_cache.cpp
                         vote_processor.cpp bootstrap.cpp)

target_link_libraries(slow_test test_common)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
