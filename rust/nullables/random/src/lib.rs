use rand::{rngs::ThreadRng, CryptoRng, RngCore};

pub struct NullableRng {
    strategy: RngStrategy,
}

impl NullableRng {
    pub fn new_null() -> Self {
        Self::new_null_u64(42)
    }

    pub fn new_null_u64(val: u64) -> Self {
        Self {
            strategy: RngStrategy::Nulled(RngStub::new(val.to_be_bytes().to_vec())),
        }
    }

    pub fn new_null_bytes(bytes: &[u8]) -> Self {
        Self {
            strategy: RngStrategy::Nulled(RngStub::new(bytes.to_vec())),
        }
    }

    pub fn thread_rng() -> Self {
        Self {
            strategy: RngStrategy::Thread(rand::thread_rng()),
        }
    }

    fn as_rng_core(&mut self) -> &mut dyn RngCore {
        match &mut self.strategy {
            RngStrategy::Thread(i) => i,
            RngStrategy::Nulled(i) => i,
        }
    }
}

impl RngCore for NullableRng {
    fn next_u32(&mut self) -> u32 {
        self.as_rng_core().next_u32()
    }

    fn next_u64(&mut self) -> u64 {
        self.as_rng_core().next_u64()
    }

    fn fill_bytes(&mut self, dest: &mut [u8]) {
        self.as_rng_core().fill_bytes(dest)
    }

    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), rand::Error> {
        self.as_rng_core().try_fill_bytes(dest)
    }
}

enum RngStrategy {
    Thread(ThreadRng),
    Nulled(RngStub),
}

impl CryptoRng for NullableRng {}

struct RngStub {
    data: Vec<u8>,
    index: usize,
}

impl RngStub {
    pub fn new(data: Vec<u8>) -> Self {
        Self { data, index: 0 }
    }
}

impl RngCore for RngStub {
    fn next_u32(&mut self) -> u32 {
        let mut buf = [0u8; 4];
        self.fill_bytes(&mut buf);
        u32::from_be_bytes(buf)
    }

    fn next_u64(&mut self) -> u64 {
        let mut buf = [0u8; 8];
        self.fill_bytes(&mut buf);
        u64::from_be_bytes(buf)
    }

    fn fill_bytes(&mut self, dest: &mut [u8]) {
        for i in 0..dest.len() {
            dest[i] = self.data[self.index];
            self.index += 1;
            if self.index >= self.data.len() {
                self.index = 0;
            }
        }
    }

    fn try_fill_bytes(&mut self, dest: &mut [u8]) -> Result<(), rand::Error> {
        self.fill_bytes(dest);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn real_rng() {
        let mut rng = NullableRng::thread_rng();
        let a1 = rng.next_u64();
        let a2 = rng.next_u64();
        let a3 = rng.next_u64();
        assert!(a1 > 0 || a2 > 0 || a3 > 0);

        let b1 = rng.next_u32();
        let b2 = rng.next_u32();
        let b3 = rng.next_u32();
        assert!(b1 > 0 || b2 > 0 || b3 > 0);

        let mut buffer = [0; 32];

        rng.fill_bytes(&mut buffer);
        assert_eq!(buffer.iter().all(|&b| b == 0), false);

        buffer = [0; 32];
        rng.try_fill_bytes(&mut buffer).unwrap();
        assert_eq!(buffer.iter().all(|&b| b == 0), false);
    }

    #[test]
    fn nullable_with_u64() {
        let mut rng = NullableRng::new_null();
        assert_eq!(rng.next_u64(), 42);

        assert_eq!(rng.next_u32(), 0);
        assert_eq!(rng.next_u32(), 42);

        let mut buffer = [0; 32];
        rng.fill_bytes(&mut buffer);
        assert_eq!(
            buffer,
            [
                0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0,
                0, 0, 0, 0, 42,
            ]
        );

        buffer = [0; 32];
        rng.try_fill_bytes(&mut buffer).unwrap();
        assert_eq!(
            buffer,
            [
                0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0,
                0, 0, 0, 0, 42,
            ]
        );
    }

    #[test]
    fn nullable_with_byte_slice() {
        let mut rng = NullableRng::new_null_bytes(&[1, 2, 3, 4, 5, 6]);
        let mut buffer = [0; 10];
        rng.fill_bytes(&mut buffer);
        assert_eq!(buffer, [1, 2, 3, 4, 5, 6, 1, 2, 3, 4]);
    }
}
