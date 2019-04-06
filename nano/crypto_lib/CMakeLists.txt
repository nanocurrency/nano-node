add_library (crypto_lib
	interface.cpp
	randompool.cpp
	randompool.hpp)

 target_link_libraries (crypto_lib
	blake2
	${CRYPTOPP_LIBRARY})
