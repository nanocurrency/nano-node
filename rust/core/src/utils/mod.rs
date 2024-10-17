mod container_info;
mod json;
mod stream;

pub use container_info::{ContainerInfo, ContainerInfoComponent};
pub use json::*;
use std::{
    net::{Ipv6Addr, SocketAddrV6},
    thread::available_parallelism,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
pub use stream::*;

pub trait Serialize {
    fn serialize(&self, stream: &mut dyn BufferWriter);
}

pub trait FixedSizeSerialize: Serialize {
    fn serialized_size() -> usize;
}

pub trait Deserialize {
    type Target;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self::Target>;
}

impl Serialize for u64 {
    fn serialize(&self, stream: &mut dyn BufferWriter) {
        stream.write_u64_be_safe(*self)
    }
}

impl FixedSizeSerialize for u64 {
    fn serialized_size() -> usize {
        std::mem::size_of::<u64>()
    }
}

impl Deserialize for u64 {
    type Target = Self;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<u64> {
        stream.read_u64_be()
    }
}

impl Serialize for [u8; 64] {
    fn serialize(&self, stream: &mut dyn BufferWriter) {
        stream.write_bytes_safe(self)
    }
}

impl FixedSizeSerialize for [u8; 64] {
    fn serialized_size() -> usize {
        64
    }
}

impl Deserialize for [u8; 64] {
    type Target = Self;

    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self::Target> {
        let mut buffer = [0; 64];
        stream.read_bytes(&mut buffer, 64)?;
        Ok(buffer)
    }
}

pub fn get_cpu_count() -> usize {
    // Try to read overridden value from environment variable
    let value = std::env::var("NANO_HARDWARE_CONCURRENCY")
        .unwrap_or_else(|_| "0".into())
        .parse::<usize>()
        .unwrap_or_default();

    if value > 0 {
        return value;
    }

    available_parallelism().unwrap().get()
}

pub type MemoryIntensiveInstrumentationCallback = extern "C" fn() -> bool;

pub static mut MEMORY_INTENSIVE_INSTRUMENTATION: Option<MemoryIntensiveInstrumentationCallback> =
    None;

extern "C" fn default_is_sanitizer_build_callback() -> bool {
    false
}
pub static mut IS_SANITIZER_BUILD: MemoryIntensiveInstrumentationCallback =
    default_is_sanitizer_build_callback;

pub fn memory_intensive_instrumentation() -> bool {
    match std::env::var("NANO_MEMORY_INTENSIVE") {
        Ok(val) => matches!(val.to_lowercase().as_str(), "1" | "true" | "on"),
        Err(_) => unsafe {
            match MEMORY_INTENSIVE_INSTRUMENTATION {
                Some(f) => f(),
                None => false,
            }
        },
    }
}

pub fn is_sanitizer_build() -> bool {
    unsafe { IS_SANITIZER_BUILD() }
}

pub fn nano_seconds_since_epoch() -> u64 {
    system_time_as_nanoseconds(SystemTime::now())
}

pub fn milliseconds_since_epoch() -> u64 {
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}

pub fn seconds_since_epoch() -> u64 {
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap()
        .as_secs()
}

pub fn system_time_from_nanoseconds(nanos: u64) -> SystemTime {
    SystemTime::UNIX_EPOCH + Duration::from_nanos(nanos)
}

pub fn system_time_as_nanoseconds(time: SystemTime) -> u64 {
    time.duration_since(SystemTime::UNIX_EPOCH)
        .expect("Time went backwards")
        .as_nanos() as u64
}

pub fn get_env_or_default<T>(variable_name: &str, default: T) -> T
where
    T: core::str::FromStr + Copy,
{
    std::env::var(variable_name)
        .map(|v| v.parse::<T>().unwrap_or(default))
        .unwrap_or(default)
}

pub fn get_env_or_default_string(variable_name: &str, default: impl Into<String>) -> String {
    std::env::var(variable_name).unwrap_or_else(|_| default.into())
}

pub fn get_env_bool(variable_name: impl AsRef<str>) -> Option<bool> {
    let variable_name = variable_name.as_ref();
    std::env::var(variable_name)
        .ok()
        .map(|val| match val.to_lowercase().as_ref() {
            "1" | "true" | "on" => true,
            "0" | "false" | "off" => false,
            _ => panic!("Invalid environment boolean value: {variable_name} = {val}"),
        })
}

pub trait Latch: Send + Sync {
    fn wait(&self);
}

pub struct NullLatch {}

impl NullLatch {
    pub fn new() -> Self {
        Self {}
    }
}

impl Latch for NullLatch {
    fn wait(&self) {}
}

pub fn parse_endpoint(s: &str) -> SocketAddrV6 {
    s.parse().unwrap()
}

pub const NULL_ENDPOINT: SocketAddrV6 = SocketAddrV6::new(Ipv6Addr::UNSPECIFIED, 0, 0, 0);

pub const TEST_ENDPOINT_1: SocketAddrV6 =
    SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0xffff, 0x10, 0, 0, 1), 1111, 0, 0);

pub const TEST_ENDPOINT_2: SocketAddrV6 =
    SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0xffff, 0x10, 0, 0, 2), 2222, 0, 0);

pub const TEST_ENDPOINT_3: SocketAddrV6 =
    SocketAddrV6::new(Ipv6Addr::new(0, 0, 0, 0xffff, 0x10, 0, 0, 3), 3333, 0, 0);

pub fn new_test_timestamp() -> SystemTime {
    UNIX_EPOCH + Duration::from_secs(1_000_000)
}
