#pragma once

#include <nano/lib/utility.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace nano
{
constexpr size_t sizeof_unchecked_info = 258;
constexpr size_t sizeof_unchecked_key = 128;

template <typename DATA_TYPE, size_t MAX_SIZE>
struct DHT_val
{
	using data_type = DATA_TYPE;
	constexpr size_t max_size ()
	{
		return MAX_SIZE;
	}
	DHT_val () = default;
	DHT_val (DHT_val const &) = default;
	DHT_val (DATA_TYPE * data_a, size_t size_a) :
		size (size_a)
	{
		release_assert (size_a <= max_size ());
		release_assert (data_a != nullptr);
		std::memcpy (data, data_a, size_a * sizeof (data_type));
	}
	DHT_val (void * data_a, size_t size_a) :
		DHT_val ((data_type *)data_a, size_a)
	{
	}
	DATA_TYPE data[MAX_SIZE];
	size_t size = 0;
};

template <typename Val>
class db_val;

using DHT_unchecked_key = DHT_val<uint8_t, sizeof_unchecked_key>;
using DHT_unchecked_info = DHT_val<uint8_t, sizeof_unchecked_info>;
using unchecked_key_dht_val = db_val<DHT_unchecked_key>;
using unchecked_info_dht_val = db_val<DHT_unchecked_info>;
}
