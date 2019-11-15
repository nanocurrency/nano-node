add_library (crypto_lib
	interface.cpp
	random_pool.cpp
	random_pool.hpp)

 target_link_libraries (crypto_lib
	blake2
	${CRYPTOPP_LIBRARY})
