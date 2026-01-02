#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/messages/message.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

namespace nano::messages
{
enum class telemetry_maker : uint8_t
{
	nf_node = 0,
	nf_pruned_node = 1
};

class telemetry_data
{
public:
	nano::signature signature{ 0 };
	nano::account node_id{};
	uint64_t block_count{ 0 };
	uint64_t cemented_count{ 0 };
	uint64_t unchecked_count{ 0 };
	uint64_t account_count{ 0 };
	uint64_t bandwidth_cap{ 0 };
	uint64_t uptime{ 0 };
	uint32_t peer_count{ 0 };
	uint8_t protocol_version{ 0 };
	nano::block_hash genesis_block{ 0 };
	uint8_t major_version{ 0 };
	uint8_t minor_version{ 0 };
	uint8_t patch_version{ 0 };
	uint8_t pre_release_version{ 0 };
	uint8_t maker{ static_cast<std::underlying_type_t<telemetry_maker>> (telemetry_maker::nf_node) }; // Where this telemetry information originated
	std::chrono::system_clock::time_point timestamp;
	uint64_t active_difficulty{ 0 };
	std::vector<uint8_t> unknown_data;

	void serialize (nano::stream &) const;
	void deserialize (nano::stream &, uint16_t);
	nano::error serialize_json (nano::jsonconfig &, bool) const;
	nano::error deserialize_json (nano::jsonconfig &, bool);
	void sign (nano::keypair const &);
	bool validate_signature () const;
	bool operator== (telemetry_data const &) const;
	bool operator!= (telemetry_data const &) const;

	// Size does not include unknown_data
	static auto constexpr size = sizeof (signature) + sizeof (node_id) + sizeof (block_count) + sizeof (cemented_count) + sizeof (unchecked_count) + sizeof (account_count) + sizeof (bandwidth_cap) + sizeof (peer_count) + sizeof (protocol_version) + sizeof (uptime) + sizeof (genesis_block) + sizeof (major_version) + sizeof (minor_version) + sizeof (patch_version) + sizeof (pre_release_version) + sizeof (maker) + sizeof (uint64_t) + sizeof (active_difficulty);
	static auto constexpr latest_size = size; // This needs to be updated for each new telemetry version

private:
	void serialize_without_signature (nano::stream &) const;

public: // Logging
	void operator() (nano::object_stream &) const;
};

class telemetry_req final : public message
{
public:
	explicit telemetry_req (nano::network_constants const & constants);
	explicit telemetry_req (message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;

public: // Logging
	void operator() (nano::object_stream &) const override;
};

class telemetry_ack final : public message
{
public:
	explicit telemetry_ack (nano::network_constants const & constants);
	telemetry_ack (bool &, nano::stream &, message_header const &);
	telemetry_ack (nano::network_constants const & constants, telemetry_data const &);
	void serialize (nano::stream &) const override;
	void visit (message_visitor &) const override;
	bool deserialize (nano::stream &);
	uint16_t size () const;
	bool is_empty_payload () const;
	static uint16_t size (message_header const &);
	telemetry_data data;

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
