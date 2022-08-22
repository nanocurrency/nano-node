#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>

using namespace std::chrono_literals;

nano::confirmation_solicitor::confirmation_solicitor (nano::network & network_a, nano::node_config const & config_a) :
	max_block_broadcasts (config_a.network_params.network.is_dev_network () ? 4 : 30),
	max_election_requests (50),
	max_election_broadcasts (std::max<std::size_t> (network_a.fanout () / 2, 1)),
	network (network_a),
	config (config_a)
{
}

void nano::confirmation_solicitor::prepare (std::vector<nano::representative> const & representatives_a)
{
	debug_assert (!prepared);
	requests.clear ();
	rebroadcasted = 0;
	/** Two copies are required as representatives can be erased from \p representatives_requests */
	representatives_requests = representatives_a;
	representatives_broadcasts = representatives_a;
	prepared = true;
}

bool nano::confirmation_solicitor::broadcast (std::shared_ptr<nano::block> const & winner, std::unordered_map<nano::account, nano::vote_info> const & last_votes)
{
	debug_assert (winner != nullptr);
	debug_assert (prepared);

	bool error (true);
	if (rebroadcasted++ < max_block_broadcasts)
	{
		auto const & hash = winner->hash ();
		nano::publish publish_winner{ config.network_params.network, winner };
		unsigned count = 0;
		// Directed broadcasting to principal representatives
		for (auto i (representatives_broadcasts.begin ()), n (representatives_broadcasts.end ()); i != n && count < max_election_broadcasts; ++i)
		{
			const auto existing = last_votes.find (i->account);
			const bool exists = existing != last_votes.end ();
			const bool different = (exists && existing->second.hash != hash);
			if (!exists || different)
			{
				i->channel->send (publish_winner);
				count += different ? 0 : 1;
			}
		}
		// Random flood for block propagation
		network.flood_message (publish_winner, nano::buffer_drop_policy::limiter, 0.5f);
		error = false;
	}
	return error;
}

bool nano::confirmation_solicitor::add (std::shared_ptr<nano::block> const & winner, std::unordered_map<nano::account, nano::vote_info> const & last_votes)
{
	debug_assert (winner != nullptr);
	debug_assert (prepared);

	bool error (true);
	unsigned count = 0;
	auto const & hash = winner->hash ();
	for (auto i (representatives_requests.begin ()); i != representatives_requests.end () && count < max_election_requests;)
	{
		bool full_queue (false);
		auto rep (*i);
		const auto existing = last_votes.find (rep.account);
		const bool exists = (existing != last_votes.end ());
		const bool is_final = (exists && existing->second.timestamp == std::numeric_limits<uint64_t>::max ());
		const bool different = (exists && existing->second.hash != hash);
		if (!exists || !is_final || different)
		{
			auto & request_queue (requests[rep.channel]);
			if (!rep.channel->max ())
			{
				request_queue.emplace_back (winner->hash (), winner->root ());
				count += different ? 0 : 1;
				error = false;
			}
			else
			{
				full_queue = true;
			}
		}
		i = !full_queue ? i + 1 : representatives_requests.erase (i);
	}
	return error;
}

void nano::confirmation_solicitor::flush ()
{
	debug_assert (prepared);
	for (auto const & request_queue : requests)
	{
		auto const & channel (request_queue.first);
		std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes_l;
		for (auto const & root_hash : request_queue.second)
		{
			roots_hashes_l.push_back (root_hash);
			if (roots_hashes_l.size () == nano::network::confirm_req_hashes_max)
			{
				nano::confirm_req req{ config.network_params.network, roots_hashes_l };
				channel->send (req);
				roots_hashes_l.clear ();
			}
		}
		if (!roots_hashes_l.empty ())
		{
			nano::confirm_req req{ config.network_params.network, roots_hashes_l };
			channel->send (req);
		}
	}
	prepared = false;
}
