add_executable(fuzz_network fuzz_network.cpp)
# -ggdb3 -O2 
target_compile_options(fuzz_network PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_network PRIVATE -fsanitize=fuzzer node)
