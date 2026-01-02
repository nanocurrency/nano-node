#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/messages/message.hpp>

#include <cstdint>

namespace nano::messages
{
class bulk_pull final : public message
{
public:
	using count_t = uint32_t;
	explicit bulk_pull (nano::network_constants const & constants);
	bulk_pull (bool &, nano::stream &, message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;
	nano::hash_or_account start{ 0 };
	nano::block_hash end{ 0 };
	count_t count{ 0 };
	bool is_count_present () const;
	void set_count_present (bool);
	static std::size_t constexpr count_present_flag = message_header::bulk_pull_count_present_flag;
	static std::size_t constexpr extended_parameters_size = 8;
	static std::size_t constexpr size = sizeof (start) + sizeof (end);

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
