use crate::{serialize_32_byte_string, u256_struct};
use ctr::cipher::{KeyIvInit, StreamCipher};
use rand::{thread_rng, Rng};
use std::ops::BitXorAssign;

type Aes256Ctr = ctr::Ctr64BE<aes::Aes256>;

u256_struct!(RawKey);
serialize_32_byte_string!(RawKey);

impl RawKey {
    pub fn random() -> Self {
        Self::from_bytes(thread_rng().gen())
    }

    pub fn encrypt(&self, key: &RawKey, iv: &[u8; 16]) -> Self {
        let mut cipher = Aes256Ctr::new(&(*key.as_bytes()).into(), &(*iv).into());
        let mut buf = self.0;
        cipher.apply_keystream(&mut buf);
        RawKey(buf)
    }

    pub fn decrypt(&self, key: &RawKey, iv: &[u8; 16]) -> Self {
        self.encrypt(key, iv)
    }

    /// IV for Key encryption
    pub fn initialization_vector_low(&self) -> [u8; 16] {
        self.0[..16].try_into().unwrap()
    }

    /// IV for Key encryption
    pub fn initialization_vector_high(&self) -> [u8; 16] {
        self.0[16..].try_into().unwrap()
    }
}

impl BitXorAssign for RawKey {
    fn bitxor_assign(&mut self, rhs: Self) {
        for (a, b) in self.0.iter_mut().zip(rhs.0) {
            *a ^= b;
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::{KeyPair, PublicKey};

    use super::*;

    #[test]
    fn encrypt() {
        let clear_text = RawKey::from(1);
        let key = RawKey::from(2);
        let iv: u128 = 123;
        let encrypted = RawKey::encrypt(&clear_text, &key, &iv.to_be_bytes());
        let expected =
            RawKey::decode_hex("3ED412A6F9840EA148EAEE236AFD10983D8E11326B07DFB33C5E1C47000AF3FD")
                .unwrap();
        assert_eq!(encrypted, expected)
    }

    #[test]
    fn encrypt_and_decrypt() {
        let clear_text = RawKey::from(1);
        let key = RawKey::from(2);
        let iv: u128 = 123;
        let encrypted = clear_text.encrypt(&key, &iv.to_be_bytes());
        let decrypted = encrypted.decrypt(&key, &iv.to_be_bytes());
        assert_eq!(decrypted, clear_text)
    }

    #[test]
    fn key_encryption() {
        let keypair = KeyPair::new();
        let secret_key = RawKey::zero();
        let iv = keypair.public_key().initialization_vector();
        let encrypted = keypair.private_key().encrypt(&secret_key, &iv);
        let decrypted = encrypted.decrypt(&secret_key, &iv);
        assert_eq!(keypair.private_key(), decrypted);
        let decrypted_pub = PublicKey::try_from(&decrypted).unwrap();
        assert_eq!(keypair.public_key(), decrypted_pub);
    }

    #[test]
    fn encrypt_produces_same_result_every_time() {
        let secret = RawKey::zero();
        let number = RawKey::from(1);
        let iv = [1; 16];
        let encrypted1 = number.encrypt(&secret, &iv);
        let encrypted2 = number.encrypt(&secret, &iv);
        assert_eq!(encrypted1, encrypted2);
    }
}
