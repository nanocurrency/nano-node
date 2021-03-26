#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/active_transactions.hpp>

#include <boost/optional.hpp>

#include <memory>

namespace nano
{
class block;
class node;
class election_scheduler final
{
public:
	election_scheduler (nano::node & node) :
		node{ node }
	{}
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void insert (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none, nano::election_behavior = nano::election_behavior::normal, std::function<void(std::shared_ptr<nano::block> const&)> const & = nullptr);
	// Activates the first unconfirmed block of \p account_a
	void activate (nano::account const &);
private:
	nano::node & node;
};
}
