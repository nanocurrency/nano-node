#pragma once

#include <nano/node/jsonpaymentobserver.hpp>
#include <nano/node/nodeobservers.hpp>

namespace nano
{
class payment_observer_processor final
{
public:
	explicit payment_observer_processor (nano::node_observers::blocks_t & blocks);
	void observer_action (nano::account const & account_a);
	void add (nano::account const & account_a, std::shared_ptr<nano::json_payment_observer> payment_observer_a);
	void erase (nano::account & account_a);

private:
	std::mutex mutex;
	std::unordered_map<nano::account, std::shared_ptr<nano::json_payment_observer>> payment_observers;
};
}
