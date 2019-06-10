#include <nano/node/node.hpp>
#include <nano/node/online_reps.hpp>

#include <cassert>

nano::online_reps::online_reps (nano::node & node_a, nano::uint128_t minimum_a) :
node (node_a),
minimum (minimum_a)
{
	auto transaction (node.ledger.store.tx_begin_read ());
	online = trend (transaction);
}

void nano::online_reps::observe (nano::account const & rep_a)
{
	auto transaction (node.ledger.store.tx_begin_read ());
	if (node.ledger.weight (transaction, rep_a) > 0)
	{
		std::lock_guard<std::mutex> lock (mutex);
		reps.insert (rep_a);
	}
}

void nano::online_reps::sample ()
{
	auto transaction (node.ledger.store.tx_begin_write ());
	// Discard oldest entries
	while (node.ledger.store.online_weight_count (transaction) >= node.network_params.node.max_weight_samples)
	{
		auto oldest (node.ledger.store.online_weight_begin (transaction));
		assert (oldest != node.ledger.store.online_weight_end ());
		node.ledger.store.online_weight_del (transaction, oldest->first);
	}
	// Calculate current active rep weight
	nano::uint128_t current;
	std::unordered_set<nano::account> reps_copy;
	{
		std::lock_guard<std::mutex> lock (mutex);
		reps_copy.swap (reps);
	}
	for (auto & i : reps_copy)
	{
		current += node.ledger.weight (transaction, i);
	}
	node.ledger.store.online_weight_put (transaction, std::chrono::system_clock::now ().time_since_epoch ().count (), current);
	auto trend_l (trend (transaction));
	std::lock_guard<std::mutex> lock (mutex);
	online = trend_l;
}

nano::uint128_t nano::online_reps::trend (nano::transaction & transaction_a)
{
	std::vector<nano::uint128_t> items;
	items.reserve (node.network_params.node.max_weight_samples + 1);
	items.push_back (minimum);
	for (auto i (node.ledger.store.online_weight_begin (transaction_a)), n (node.ledger.store.online_weight_end ()); i != n; ++i)
	{
		items.push_back (i->second.number ());
	}

	// Pick median value for our target vote weight
	auto median_idx = items.size () / 2;
	nth_element (items.begin (), items.begin () + median_idx, items.end ());
	return nano::uint128_t{ items[median_idx] };
}

nano::uint128_t nano::online_reps::online_stake () const
{
	std::lock_guard<std::mutex> lock (mutex);
	return std::max (online, minimum);
}

std::vector<nano::account> nano::online_reps::list ()
{
	std::vector<nano::account> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto & i : reps)
	{
		result.push_back (i);
	}
	return result;
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (online_reps.mutex);
		count = online_reps.reps.size ();
	}

	auto sizeof_element = sizeof (decltype (online_reps.reps)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "arrival", count, sizeof_element }));
	return composite;
}
}
