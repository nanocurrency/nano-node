#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/messages/message.hpp>
#include <nano/messages/message_type.hpp>

namespace nano::messages
{
class bulk_pull_account final : public message
{
public:
	explicit bulk_pull_account (nano::network_constants const & constants);
	bulk_pull_account (bool &, nano::stream &, message_header const &);
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void visit (message_visitor &) const override;
	nano::account account;
	nano::amount minimum_amount;
	bulk_pull_account_flags flags;
	static std::size_t constexpr size = sizeof (account) + sizeof (minimum_amount) + sizeof (bulk_pull_account_flags);

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}
