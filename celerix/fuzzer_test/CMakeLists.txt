add_executable(fuzz_buffer fuzz_buffer.cpp)
target_compile_options(fuzz_buffer PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_buffer PRIVATE -fsanitize=fuzzer node gtest)

add_executable(fuzz_bignum fuzz_bignum.cpp)
target_compile_options(fuzz_bignum PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_bignum PRIVATE -fsanitize=fuzzer node gtest)

add_executable(fuzz_endpoint_parsing fuzz_endpoint_parsing.cpp)
target_compile_options(fuzz_endpoint_parsing PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_endpoint_parsing PRIVATE -fsanitize=fuzzer node
                                                    gtest)
