#pragma once

#include <nano/lib/network_filter.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/message.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nano::messages
{
/*
 * Binary Format:
 * [message_header] Common message header
 * [N x (32 bytes (block hash) + 32 bytes (root))] Pairs of (block_hash, root)
 * - The count is determined by the header's count bits.
 *
 * Header extensions:
 * - [0xf000] Count (for V1 protocol)
 * - [0x0f00] Block type
 *   - Not used anymore (V25.1+), but still present and set to `not_a_block = 0x1` for backwards compatibility
 * - [0xf000 (high), 0x00f0 (low)] Count V2 (for V2 protocol)
 * - [0x0001] Confirm V2 flag
 * - [0x0002] Reserved for V3+ versioning
 */
class confirm_req final : public message
{
public:
	confirm_req (bool & error, nano::stream &, message_header const &);
	confirm_req (nano::network_constants const & constants, std::vector<std::pair<nano::block_hash, nano::root>> const &);
	confirm_req (nano::network_constants const & constants, nano::block_hash const &, nano::root const &);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;
	bool operator== (confirm_req const &) const;
	std::string roots_string () const;

	static std::size_t size (message_header const &);

private:
	static uint8_t hash_count (message_header const &);

public: // Payload
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;

public: // Logging
	void operator() (nano::object_stream &) const override;
};

/*
 * Binary Format:
 * [message_header] Common message header
 * [variable] Vote
 * - Serialized/deserialized by the `nano::vote` class.
 *
 * Header extensions:
 * - [0xf000] Count (for V1 protocol)
 * - [0x0f00] Block type
 *   - Not used anymore (V25.1+), but still present and set to `not_a_block = 0x1` for backwards compatibility
 * - [0xf000 (high), 0x00f0 (low)] Count V2 masks (for V2 protocol)
 * - [0x0001] Confirm V2 flag
 * - [0x0002] Reserved for V3+ versioning
 * - [0x0004] Rebroadcasted flag
 */
class confirm_ack final : public message
{
public:
	confirm_ack (bool & error, nano::stream &, message_header const &, nano::network_filter::digest_t const & digest = 0, nano::vote_uniquer * = nullptr);
	confirm_ack (nano::network_constants const & constants, std::shared_ptr<nano::vote> const &, bool rebroadcasted = false);

	void serialize (nano::stream &) const override;
	void visit (message_visitor &) const override;
	bool operator== (confirm_ack const &) const;

	static std::size_t size (message_header const &);

	static uint8_t constexpr rebroadcasted_flag = 2; // 0x0004
	bool is_rebroadcasted () const;

private:
	static uint8_t hash_count (message_header const &);

public: // Payload
	std::shared_ptr<nano::vote> vote;

	// Messages deserialized from network should have their digest set
	nano::network_filter::digest_t digest{ 0 };

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
