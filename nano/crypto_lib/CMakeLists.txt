add_library(
  crypto_lib interface.cpp random_pool.hpp random_pool.cpp
             random_pool_shuffle.hpp secure_memory.hpp secure_memory.cpp)

target_link_libraries(crypto_lib blake2 ${CRYPTOPP_LIBRARY})

include_directories(${CMAKE_SOURCE_DIR}/submodules)
