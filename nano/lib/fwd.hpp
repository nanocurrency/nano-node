#pragma once

#include <cstdint>
#include <iosfwd>

struct uint8_char_traits;

namespace nano
{
class block;
class block_details;
class block_visitor;
class container_info;
class error;
class jsonconfig;
class logger;
class mutable_block_visitor;
class network_constants;
class object_stream;
class root;
class stats;
class thread_pool;
class thread_runner;
class tomlconfig;
template <typename Key, typename Value>
class uniquer;

enum class block_type : uint8_t;
enum class epoch : uint8_t;
enum class work_version;

using stream = std::basic_streambuf<uint8_t, uint8_char_traits>;
}

namespace nano::stat
{
enum class type;
enum class detail;
enum class dir;
}
