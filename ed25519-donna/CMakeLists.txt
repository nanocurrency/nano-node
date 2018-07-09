add_library (ed25519
	ed25519-donna-portable.h
	ed25519-hash-custom.h
	ed25519-randombytes-custom.h
	ed25519.h
	ed25519.c)

target_compile_definitions(ed25519 PUBLIC
	-DED25519_CUSTOMHASH
	-DED25519_CUSTOMRNG)
