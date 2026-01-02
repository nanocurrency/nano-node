#pragma once

#include <nano/lib/common.hpp>
#include <nano/messages/message.hpp>

#include <array>

namespace nano::messages
{
/*
 * Binary Format:
 * [message_header] Common message header
 * [8x (16 bytes (IP) + 2 bytes (port)] Array of 8 peers
 *
 * Header extensions:
 * - No specific bits from the `extensions` field are used for `keepalive`.
 */
class keepalive final : public message
{
public:
	explicit keepalive (nano::network_constants const & constants);
	keepalive (bool &, nano::stream &, message_header const &);
	void visit (message_visitor &) const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	bool operator== (keepalive const &) const;
	std::array<nano::endpoint, 8> peers;
	static std::size_t constexpr size = 8 * (16 + 2);

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
