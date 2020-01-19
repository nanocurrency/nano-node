add_executable(fuzz_buffer fuzz_buffer.cpp)
target_compile_options(fuzz_buffer PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_buffer PRIVATE -fsanitize=fuzzer node)
