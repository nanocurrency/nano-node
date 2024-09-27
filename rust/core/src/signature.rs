use serde::de::{Unexpected, Visitor};
use crate::utils::{BufferWriter, Serialize, Stream};
use std::fmt::Write;

#[derive(Clone, PartialEq, Eq, Debug, PartialOrd, Ord)]
pub struct Signature {
    bytes: [u8; 64],
}

impl Default for Signature {
    fn default() -> Self {
        Self { bytes: [0; 64] }
    }
}

impl Signature {
    pub fn new() -> Self {
        Self { bytes: [0u8; 64] }
    }

    pub fn from_bytes(bytes: [u8; 64]) -> Self {
        Self { bytes }
    }

    pub fn try_from_bytes(bytes: &[u8]) -> anyhow::Result<Self> {
        Ok(Self::from_bytes(bytes.try_into()?))
    }

    pub const fn serialized_size() -> usize {
        64
    }

    pub fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Signature> {
        let mut result = Signature { bytes: [0; 64] };

        stream.read_bytes(&mut result.bytes, 64)?;
        Ok(result)
    }

    pub fn as_bytes(&'_ self) -> &'_ [u8; 64] {
        &self.bytes
    }

    pub unsafe fn copy_bytes(&self, target: *mut u8) {
        let bytes = std::slice::from_raw_parts_mut(target, 64);
        bytes.copy_from_slice(self.as_bytes());
    }

    pub fn make_invalid(&mut self) {
        self.bytes[31] ^= 1;
    }

    pub fn encode_hex(&self) -> String {
        let mut result = String::with_capacity(128);
        for byte in self.bytes {
            write!(&mut result, "{:02X}", byte).unwrap();
        }
        result
    }

    pub fn decode_hex(s: impl AsRef<str>) -> anyhow::Result<Self> {
        let mut bytes = [0u8; 64];
        hex::decode_to_slice(s.as_ref(), &mut bytes)?;
        Ok(Signature::from_bytes(bytes))
    }

    pub unsafe fn from_ptr(ptr: *const u8) -> Self {
        let mut bytes = [0; 64];
        bytes.copy_from_slice(std::slice::from_raw_parts(ptr, 64));
        Signature::from_bytes(bytes)
    }
}

impl Serialize for Signature {
    fn serialize(&self, writer: &mut dyn BufferWriter) {
        writer.write_bytes_safe(&self.bytes)
    }
}

impl serde::Serialize for Signature {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        serializer.serialize_str(&self.encode_hex())
    }
}

impl<'de> serde::Deserialize<'de> for Signature {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let value = deserializer.deserialize_str(SignatureVisitor {})?;
        Ok(value)
    }
}

pub(crate) struct SignatureVisitor {}

impl<'de> Visitor<'de> for SignatureVisitor {
    type Value = Signature;

    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("a hex string containing 64 bytes")
    }

    fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
    where
        E: serde::de::Error,
    {
        let signature = Signature::decode_hex(v).map_err(|_| {
            serde::de::Error::invalid_value(Unexpected::Str(v), &"a hex string containing 64 bytes")
        })?;
        Ok(signature)
    }
}
