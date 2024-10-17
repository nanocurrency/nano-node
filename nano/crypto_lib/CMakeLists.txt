add_library(nano_ed25519 interface.cpp)

target_link_libraries(ed25519 nano_ed25519)

add_library(crypto_lib random_pool.hpp random_pool.cpp random_pool_shuffle.hpp
                       secure_memory.hpp secure_memory.cpp)

target_link_libraries(crypto_lib blake2 ed25519 ${CRYPTOPP_LIBRARY} rsnano_ffi)

add_dependencies(crypto_lib cargo-build_rsnano_ffi)
