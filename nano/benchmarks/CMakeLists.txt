add_executable(benchmarks entry.cpp ledger.cpp stats.cpp)

target_link_libraries(benchmarks test_common benchmark::benchmark)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
