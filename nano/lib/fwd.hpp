#pragma once

#include <cstdint>
#include <iosfwd>

struct uint8_char_traits;

namespace nano
{
enum class block_type : uint8_t;
enum class epoch : uint8_t;
enum class network_type : uint16_t;
enum class database_backend;
enum class work_version;

class uint128_union;
class uint256_union;
class uint512_union;
class amount;
class block_hash;
class fan;
class kdf;
class public_key;
class wallet_id;
class hash_or_account;
class link;
class root;
class raw_key;
class signature;
class qualified_root;
class account;

class block;
class block_details;
class block_visitor;
class container_info;
class error;
class jsonconfig;
class lmdb_config;
class keypair;
class logger;
class mutable_block_visitor;
class network_constants;
class object_stream;
class rocksdb_config;
class stats;
class stats_config;
class thread_pool;
class thread_runner;
class tomlconfig;
class vote;
class work_pool;

struct bootstrap_weights;

template <typename Key, typename Value>
class uniquer;

template <typename E>
class enum_flags;

enum class node_capabilities : uint64_t;
using node_capabilities_flags = nano::enum_flags<nano::node_capabilities>;

using stream = std::basic_streambuf<uint8_t, uint8_char_traits>;

using seconds_t = uint64_t;
using millis_t = uint64_t;
}

namespace nano::stat
{
enum class type;
enum class detail;
enum class dir;
}
