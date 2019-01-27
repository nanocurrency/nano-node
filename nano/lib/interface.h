#ifndef XRB_INTERFACE_H
#define XRB_INTERFACE_H

#if __cplusplus
extern "C" {
#endif

typedef unsigned char * xrb_uint128; // 16byte array for public and private keys
typedef unsigned char * xrb_uint256; // 32byte array for public and private keys
typedef unsigned char * xrb_uint512; // 64byte array for signatures
typedef void * xrb_transaction;
// clang-format off
// Convert amount bytes 'source' to a 40 byte null-terminated decimal string 'destination'
[[deprecated]] void xrb_uint128_to_dec (const xrb_uint128 source, char * destination);
// Convert public/private key bytes 'source' to a 65 byte null-terminated hex string 'destination'
[[deprecated]] void xrb_uint256_to_string (const xrb_uint256 source, char * destination);
// Convert public key bytes 'source' to a 66 byte non-null-terminated account string 'destination'
[[deprecated]] void xrb_uint256_to_address (xrb_uint256 source, char * destination);
// Convert public/private key bytes 'source' to a 129 byte null-terminated hex string 'destination'
[[deprecated]] void xrb_uint512_to_string (const xrb_uint512 source, char * destination);

// Convert 39 byte decimal string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
[[deprecated]] int xrb_uint128_from_dec (const char * source, xrb_uint128 destination);
// Convert 64 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
[[deprecated]] int xrb_uint256_from_string (const char * source, xrb_uint256 destination);
// Convert 128 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
[[deprecated]] int xrb_uint512_from_string (const char * source, xrb_uint512 destination);

// Check if the null-terminated string 'account' is a valid xrb account number
// Return 0 on correct, nonzero on invalid
[[deprecated]] int xrb_valid_address (const char * account);

// Create a new random number in to 'destination'
[[deprecated]] void xrb_generate_random (xrb_uint256 destination);
// Retrieve the deterministic private key for 'seed' at 'index'
[[deprecated]] void xrb_seed_key (const xrb_uint256 seed, int index, xrb_uint256);
// Derive the public key 'pub' from 'key'
[[deprecated]] void xrb_key_account (xrb_uint256 key, xrb_uint256 pub);

// Sign 'transaction' using 'private_key' and write to 'signature'
char * xrb_sign_transaction (const char * transaction, const xrb_uint256 private_key);
// Generate work for 'transaction'
[[deprecated]]
char * xrb_work_transaction (const char * transaction);
// clang-format on
#if __cplusplus
} // extern "C"
#endif

#endif // XRB_INTERFACE_H
