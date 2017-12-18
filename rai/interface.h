#ifndef XRB_INTERFACE_H
#define XRB_INTERFACE_H

#if __cplusplus
extern "C" {
#endif

typedef char * xrb_uint256; // 32byte array for public and private keys
typedef char * xrb_uint512; // 64byte array for signatures
typedef void * xrb_transaction;

// Convert public/private key bytes 'source' to a 64 byte not-null-terminated hex string 'destination'
void xrb_uint256_to_string (xrb_uint256 source, char * destination);
// Convert public/private key bytes 'source' to a 128 byte not-null-terminated hex string 'destination'
void xrb_uint512_to_string (xrb_uint512 source, char * destination);

// Convert 64 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int xrb_uint256_from_string (char * source, xrb_uint256 destination);
// Convert 128 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int xrb_uint512_from_string (char * source, xrb_uint512 destination);

// Check if the null-terminated string 'account' is a valid xrb account number
// Return 0 on correct, nonzero on invalid
int xrb_valid_address (char * account);

// Sign 'transaction' using 'private_key' and write to 'signature'
char * sign_transaction (char * transaction, xrb_uint256 private_key, xrb_uint512 signature);

#if __cplusplus
} // extern "C"
#endif

#endif // XRB_INTERFACE_H
