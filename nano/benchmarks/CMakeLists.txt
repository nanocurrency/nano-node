add_executable(benchmarks allocations.hpp allocations.cpp entry.cpp ledger.cpp
                          stats.cpp vote_processing.cpp)

target_link_libraries(benchmarks test_common benchmark::benchmark)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
