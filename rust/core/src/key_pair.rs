use super::{PublicKey, RawKey, Signature};
use anyhow::Context;
use ed25519_dalek::ed25519::signature::SignerMut;
use ed25519_dalek::Verifier;

pub fn sign_message(private_key: &RawKey, data: &[u8]) -> Signature {
    let secret = ed25519_dalek::SecretKey::from(*private_key.as_bytes());
    let mut signing_key = ed25519_dalek::SigningKey::from(&secret);
    let signature = signing_key.sign(data);
    Signature::from_bytes(signature.to_bytes())
}

pub fn validate_message(
    public_key: &PublicKey,
    message: &[u8],
    signature: &Signature,
) -> anyhow::Result<()> {
    let public = ed25519_dalek::VerifyingKey::from_bytes(public_key.as_bytes())
        .map_err(|_| anyhow!("could not extract public key"))?;
    let sig = ed25519_dalek::Signature::from_bytes(signature.as_bytes());
    public
        .verify(message, &sig)
        .map_err(|_| anyhow!("could not verify message"))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use ed25519_dalek::ed25519::signature::SignerMut;
    use super::*;

    #[test]
    fn ed25519_signing() -> anyhow::Result<()> {
        let secret_key = ed25519_dalek::SecretKey::from([0u8; 32]);
        let message = [0u8; 32];
        let mut signing_key = ed25519_dalek::SigningKey::from(&secret_key);
        let public_key = ed25519_dalek::VerifyingKey::from(&signing_key);
        let signature = signing_key.sign(&message);
        public_key.verify_strict(&message, &signature).unwrap();

        let mut sig_bytes = signature.to_bytes();
        sig_bytes[32] ^= 0x1;
        let signature = ed25519_dalek::Signature::from_bytes(&sig_bytes);
        assert!(public_key.verify_strict(&message, &signature).is_err());

        Ok(())
    }

    #[test]
    fn sign_message_test() -> anyhow::Result<()> {
        let keypair = KeyPair::new();
        let data = [0u8; 32];
        let signature = sign_message(&keypair.private_key(), &data);
        validate_message(&keypair.public_key(), &data, &signature)?;
        Ok(())
    }

    #[test]
    fn signing_same_message_twice_produces_equal_signatures() {
        // the C++ implementation adds random bytes and a padding when signing for extra security and for making side channel attacks more difficult.
        // Currently the Rust impl does not do that.
        // In C++ signing the same message twice will produce different signatures. In Rust we get the same signature.
        let keypair = KeyPair::new();
        let data = [1, 2, 3];
        let signature_a = sign_message(&keypair.private_key(), &data);
        let signature_b = sign_message(&keypair.private_key(), &data);
        assert_eq!(signature_a, signature_b);
    }
}
