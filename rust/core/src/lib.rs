#[macro_use]
extern crate anyhow;

pub mod key_pair;
pub use key_pair::*;

mod utils;

pub mod signature;
pub use signature::*;

mod raw_key;
pub use raw_key::RawKey;

mod u256_struct;

u256_struct!(PublicKey);
serialize_32_byte_string!(PublicKey);

pub fn write_hex_bytes(bytes: &[u8], f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
    for &byte in bytes {
        write!(f, "{:02X}", byte)?;
    }
    Ok(())
}