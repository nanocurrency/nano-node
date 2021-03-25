#include <nano/node/election_scheduler.hpp>

#include <nano/node/node.hpp>

void nano::election_scheduler::insert (std::shared_ptr<nano::block> const & block_a, boost::optional<nano::uint128_t> const & previous_balance_a, nano::election_behavior election_behavior_a, std::function<void(std::shared_ptr<nano::block> const &)> const & confirmation_action_a)
{
	nano::unique_lock<nano::mutex> lock (node.active.mutex);
	node.active.insert_impl (lock, block_a, previous_balance_a, election_behavior_a, confirmation_action_a);
}
