add_executable(
  benchmarks
  allocations.hpp
  allocations.cpp
  entry.cpp
  ledger.cpp
  message_processing.cpp
  message_sending.cpp
  rep_weights.cpp
  stats.cpp
  vote_processing.cpp
  vote_routing.cpp)

target_link_libraries(benchmarks test_common benchmark::benchmark)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
