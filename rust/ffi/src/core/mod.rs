use rsnano_core::{sign_message, validate_message, PublicKey, RawKey, Signature};
use std::{ffi::CStr, net::Ipv6Addr, os::raw::c_char, slice};

#[no_mangle]
pub unsafe extern "C" fn rsn_sign_message(
    priv_key: *const u8,
    pub_key: *const u8,
    message: *const u8,
    len: usize,
    signature: *mut u8,
) -> i32 {
    let private_key = RawKey::from_ptr(priv_key);
    let data = if message.is_null() {
        &[]
    } else {
        std::slice::from_raw_parts(message, len)
    };
    let sig = sign_message(&private_key, data);
    let signature = slice::from_raw_parts_mut(signature, 64);
    signature.copy_from_slice(sig.as_bytes());
    0
}

#[no_mangle]
pub unsafe extern "C" fn rsn_validate_message(
    pub_key: &[u8; 32],
    message: *const u8,
    len: usize,
    signature: &[u8; 64],
) -> bool {
    let public_key = PublicKey::from_bytes(*pub_key);
    let message = if message.is_null() {
        &[]
    } else {
        std::slice::from_raw_parts(message, len)
    };
    let signature = Signature::from_bytes(*signature);
    validate_message(&public_key, message, &signature).is_err()
}

