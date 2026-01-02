#pragma once

#include <nano/lib/asio.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/messages/common.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/message_header.hpp>

#include <memory>
#include <vector>

namespace nano::messages
{
class message
{
public:
	explicit message (nano::network_constants const &, message_type);
	explicit message (message_header const &);
	virtual ~message () = default;

	virtual void serialize (nano::stream &) const = 0;
	virtual void visit (message_visitor &) const = 0;

	std::shared_ptr<std::vector<uint8_t>> to_bytes () const;
	nano::shared_const_buffer to_shared_const_buffer () const;

	message_type type () const;

public:
	message_header header;

public: // Logging
	virtual void operator() (nano::object_stream &) const;
};
}
