#pragma once

#include <nano/lib/block_type.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/networks.hpp>
#include <nano/messages/message_type.hpp>

#include <bitset>
#include <cstdint>

namespace nano::messages
{
/*
 * Common Header Binary Format:
 * [2 bytes] Network (big endian)
 * [1 byte] Maximum protocol version
 * [1 byte] Protocol version currently in use
 * [1 byte] Minimum protocol version
 * [1 byte] Message type
 * [2 bytes] Extensions (message-specific flags and properties)
 *
 * Notes:
 * - The structure and bit usage of the `extensions` field vary by message type.
 */
class message_header final
{
public:
	using extensions_bitset_t = std::bitset<16>;

	message_header (nano::network_constants const &, message_type);
	message_header (bool &, nano::stream &);

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

public: // Payload
	nano::network_type network;
	uint8_t version_max;
	uint8_t version_using;
	uint8_t version_min;
	message_type type;
	extensions_bitset_t extensions;

public:
	static std::size_t constexpr size = sizeof (nano::network_type) + sizeof (version_max) + sizeof (version_using) + sizeof (version_min) + sizeof (type) + sizeof (/* extensions */ uint16_t);

	bool flag_test (uint8_t flag) const;
	void flag_set (uint8_t flag, bool enable = true);

	nano::block_type block_type () const;
	void block_type_set (nano::block_type);

	uint8_t count_get () const;
	void count_set (uint8_t);
	uint8_t count_v2_get () const;
	void count_v2_set (uint8_t);

	static uint8_t constexpr bulk_pull_count_present_flag = 0;
	static uint8_t constexpr bulk_pull_ascending_flag = 1;
	bool bulk_pull_is_count_present () const;
	bool bulk_pull_ascending () const;

	static uint8_t constexpr frontier_req_only_confirmed = 1;
	bool frontier_req_is_only_confirmed_present () const;

	static uint8_t constexpr confirm_v2_flag = 0;
	bool confirm_is_v2 () const;
	void confirm_set_v2 (bool);

	/** Size of the payload in bytes. For some messages, the payload size is based on header flags. */
	std::size_t payload_length_bytes () const;
	bool is_valid_message_type () const;

	static extensions_bitset_t constexpr block_type_mask{ 0x0f00 };
	static extensions_bitset_t constexpr count_mask{ 0xf000 };
	static extensions_bitset_t constexpr count_v2_mask_left{ 0xf000 };
	static extensions_bitset_t constexpr count_v2_mask_right{ 0x00f0 };
	static extensions_bitset_t constexpr telemetry_size_mask{ 0x3ff };

public: // Logging
	void operator() (nano::object_stream &) const;
};
}
