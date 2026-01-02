#pragma once

#include <nano/lib/network_filter.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/message.hpp>

#include <memory>

namespace nano::messages
{
/*
 * Binary Format:
 * [message_header] Common message header
 * [variable] Block (serialized according to the block type specified in the header)
 *
 * Header extensions:
 * - [0x0f00] Block type: Identifies the specific type of the block.
 * - [0x0004] Originator flag
 */
class publish final : public message
{
public:
	publish (bool &, nano::stream &, message_header const &, nano::network_filter::digest_t const & digest = 0, nano::block_uniquer * = nullptr);
	publish (nano::network_constants const & constants, std::shared_ptr<nano::block> const &, bool is_originator = false);

	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &, nano::block_uniquer * = nullptr);
	void visit (message_visitor &) const override;
	bool operator== (publish const &) const;

	static uint8_t constexpr originator_flag = 2; // 0x0004
	bool is_originator () const;

public: // Payload
	std::shared_ptr<nano::block> block;

	// Messages deserialized from network should have their digest set
	nano::network_filter::digest_t digest{ 0 };

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
