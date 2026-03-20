#pragma once

#include <cstdint>
#include <iosfwd>

struct uint8_char_traits;

namespace nano
{
enum class block_type : uint8_t;
enum class epoch : uint8_t;
enum class network_type : uint16_t;
enum class work_version;

class uint128_union;
class uint256_union;
class uint512_union;
class amount;
class block_hash;
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
class keypair;
class logger;
class mutable_block_visitor;
class network_constants;
class object_stream;
class stats;
class thread_pool;
class thread_runner;
class tomlconfig;
class vote;

template <typename Key, typename Value>
class uniquer;

using stream = std::basic_streambuf<uint8_t, uint8_char_traits>;
}

namespace nano::stat
{
enum class type;
enum class detail;
enum class dir;
}
