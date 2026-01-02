#pragma once

#include <nano/messages/message.hpp>

namespace nano::messages
{
class bulk_push final : public message
{
public:
	explicit bulk_push (nano::network_constants const & constants);
	explicit bulk_push (message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
