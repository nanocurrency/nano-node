use serde::de::{Unexpected, Visitor};

#[macro_export]
macro_rules! u256_struct {
    ($name:ident) => {
        #[derive(PartialEq, Eq, Clone, Copy, Hash, Default, PartialOrd, Ord)]
        pub struct $name([u8; 32]);

        #[allow(dead_code)]
        impl $name {
            pub const fn zero() -> Self {
                Self([0; 32])
            }

            pub fn is_zero(&self) -> bool {
                self.0 == [0; 32]
            }

            pub const fn from_bytes(bytes: [u8; 32]) -> Self {
                Self(bytes)
            }

            pub fn from_slice(bytes: &[u8]) -> Option<Self> {
                match bytes.try_into() {
                    Ok(value) => Some(Self(value)),
                    Err(_) => None,
                }
            }

            pub fn as_bytes(&'_ self) -> &'_ [u8; 32] {
                &self.0
            }

            pub unsafe fn copy_bytes(&self, target: *mut u8) {
                let target_slice = std::slice::from_raw_parts_mut(target, 32);
                target_slice.copy_from_slice(self.as_bytes());
            }

            pub fn number(&self) -> primitive_types::U256 {
                primitive_types::U256::from_big_endian(&self.0)
            }

            pub fn inc(&self) -> Option<Self> {
                self.number()
                    .checked_add(primitive_types::U256::from(1))
                    .map(|i| Self::from(i))
            }

            pub fn encode_hex(&self) -> String {
                use std::fmt::Write;
                let mut result = String::with_capacity(64);
                for &byte in self.as_bytes() {
                    write!(&mut result, "{:02X}", byte).unwrap();
                }
                result
            }

            pub fn decode_hex(s: impl AsRef<str>) -> anyhow::Result<Self> {
                Ok(Self::from_bytes(crate::u256_struct::decode_32_bytes_hex(
                    s,
                )?))
            }

            pub unsafe fn from_ptr(data: *const u8) -> Self {
                Self(std::slice::from_raw_parts(data, 32).try_into().unwrap())
            }
        }

        impl $crate::utils::Serialize for $name {
            fn serialize(&self, writer: &mut dyn $crate::utils::BufferWriter) {
                writer.write_bytes_safe(&self.0)
            }
        }

        impl $crate::utils::FixedSizeSerialize for $name {
            fn serialized_size() -> usize {
                32
            }
        }

        impl $crate::utils::Deserialize for $name {
            type Target = Self;
            fn deserialize(stream: &mut dyn $crate::utils::Stream) -> anyhow::Result<Self> {
                let mut result = Self::zero();
                stream.read_bytes(&mut result.0, 32)?;
                Ok(result)
            }
        }

        impl From<i32> for $name {
            fn from(value: i32) -> Self {
                let mut bytes = [0; 32];
                bytes[28..].copy_from_slice(&value.to_be_bytes());
                Self::from_bytes(bytes)
            }
        }

        impl From<u64> for $name {
            fn from(value: u64) -> Self {
                let mut bytes = [0; 32];
                bytes[24..].copy_from_slice(&value.to_be_bytes());
                Self::from_bytes(bytes)
            }
        }

        impl From<u128> for $name {
            fn from(value: u128) -> Self {
                let mut bytes = [0; 32];
                bytes[16..].copy_from_slice(&value.to_be_bytes());
                Self::from_bytes(bytes)
            }
        }

        impl From<primitive_types::U256> for $name {
            fn from(value: primitive_types::U256) -> Self {
                let mut key = Self::zero();
                value.to_big_endian();
                key
            }
        }

        impl std::fmt::Debug for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                $crate::write_hex_bytes(&self.0, f)
            }
        }

        impl std::fmt::Display for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                $crate::write_hex_bytes(&self.0, f)
            }
        }
    };
}

pub fn decode_32_bytes_hex(s: impl AsRef<str>) -> anyhow::Result<[u8; 32]> {
    let s = s.as_ref();
    if s.is_empty() || s.len() > 64 {
        bail!(
            "Invalid U256 string length. Expected <= 64 but was {}",
            s.len()
        );
    }

    let mut padded_string = String::new();
    let sanitized = if s.len() < 64 {
        for _ in 0..(64 - s.len()) {
            padded_string.push('0');
        }
        padded_string.push_str(s);
        &padded_string
    } else {
        s
    };

    let mut bytes = [0u8; 32];
    hex::decode_to_slice(sanitized, &mut bytes)?;
    Ok(bytes)
}

#[macro_export]
macro_rules! serialize_32_byte_string {
    ($name:ident) => {
        impl serde::Serialize for $name {
            fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
            where
                S: serde::Serializer,
            {
                serializer.serialize_str(&self.encode_hex())
            }
        }

        impl<'de> serde::Deserialize<'de> for $name {
            fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
            where
                D: serde::Deserializer<'de>,
            {
                let value = deserializer.deserialize_str(crate::u256_struct::U256Visitor {})?;
                Ok(Self::from_bytes(value))
            }
        }
    };
}

pub(crate) struct U256Visitor {}

impl<'de> Visitor<'de> for U256Visitor {
    type Value = [u8; 32];

    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("a hex string containing 32 bytes")
    }

    fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
    where
        E: serde::de::Error,
    {
        let bytes = decode_32_bytes_hex(v).map_err(|_| {
            serde::de::Error::invalid_value(Unexpected::Str(v), &"a hex string containing 32 bytes")
        })?;
        Ok(bytes)
    }
}

#[cfg(test)]
mod tests {
    u256_struct!(U256Test);

    #[test]
    fn constructor() {
        let x = U256Test::zero();
        assert_eq!(x.0, [0; 32]);
        assert!(x.is_zero());
    }

    #[test]
    fn encode_hex() {
        assert_eq!(
            U256Test::zero().encode_hex(),
            "0000000000000000000000000000000000000000000000000000000000000000"
        );
        assert_eq!(
            U256Test::from(0x12ab).encode_hex(),
            "00000000000000000000000000000000000000000000000000000000000012AB"
        );
        assert_eq!(
            U256Test::from_bytes([0xff; 32]).encode_hex(),
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
        );
    }
}
