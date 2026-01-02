#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/messages/message.hpp>

#include <cstdint>

namespace nano::messages
{
class frontier_req final : public message
{
public:
	explicit frontier_req (nano::network_constants const & constants);
	frontier_req (bool &, nano::stream &, message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;
	bool operator== (frontier_req const &) const;
	nano::account start;
	uint32_t age;
	uint32_t count;
	static std::size_t constexpr size = sizeof (start) + sizeof (age) + sizeof (count);

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
