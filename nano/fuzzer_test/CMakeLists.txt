add_executable(fuzz_messages fuzz_messages.cpp)
target_compile_options(fuzz_messages PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_messages PRIVATE -fsanitize=fuzzer nano_messages)

add_executable(fuzz_bignum fuzz_bignum.cpp)
target_compile_options(fuzz_bignum PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_bignum PRIVATE -fsanitize=fuzzer node gtest)

add_executable(fuzz_endpoint_parsing fuzz_endpoint_parsing.cpp)
target_compile_options(fuzz_endpoint_parsing PUBLIC -fsanitize=fuzzer)
target_link_libraries(fuzz_endpoint_parsing PRIVATE -fsanitize=fuzzer node
                                                    gtest)
