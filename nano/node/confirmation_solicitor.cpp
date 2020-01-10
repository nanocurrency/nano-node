#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>

using namespace std::chrono_literals;

nano::confirmation_solicitor::confirmation_solicitor (nano::node & node_a) :
node (node_a),
max_confirm_req_batches (node_a.network_params.network.is_test_network () ? 1 : 20),
max_block_broadcasts (node_a.network_params.network.is_test_network () ? 4 : 30),
min_time_between_requests (node_a.network_params.network.is_test_network () ? 10ms : 3s),
min_time_between_floods (node_a.network_params.network.is_test_network () ? 20ms : 6s),
min_request_count_flood (node_a.network_params.network.is_test_network () ? 0 : 4)
{
}

void nano::confirmation_solicitor::prepare (std::vector<nano::representative> const & representatives_a)
{
	assert (!prepared);
	requests.clear ();
	rebroadcasted = 0;
	// Only representatives ready to receive batched confirm_req
	representatives = representatives_a;
	prepared = true;
}

bool nano::confirmation_solicitor::add (std::shared_ptr<nano::election> election_a)
{
	assert (prepared);
	auto now (std::chrono::steady_clock::now ());
	auto flood_cutoff (now - min_time_between_floods);
	if (election_a->confirmation_request_count >= min_request_count_flood && election_a->last_broadcast < flood_cutoff && rebroadcasted++ < max_block_broadcasts)
	{
		election_a->last_broadcast = now;
		node.network.flood_block (election_a->status.winner);
	}
	static auto max_channel_requests (max_confirm_req_batches * nano::network::confirm_req_hashes_max);
	auto request_cutoff (now - min_time_between_requests);
	bool any_bundle{ false };
	if (election_a->last_request < request_cutoff)
	{
		for (auto const & rep : representatives)
		{
			if (election_a->last_votes.find (rep.account) == election_a->last_votes.end ())
			{
				auto existing (requests.find (rep.channel));
				if (existing != requests.end ())
				{
					if (existing->second.size () < max_channel_requests)
					{
						existing->second.push_back ({ election_a->status.winner->hash (), election_a->status.winner->root () });
						any_bundle = true;
					}
				}
				else
				{
					requests.emplace (rep.channel, vector_root_hashes{ { election_a->status.winner->hash (), election_a->status.winner->root () } });
					any_bundle = true;
				}
			}
		}
	}
	if (any_bundle)
	{
		election_a->last_request = now;
		++election_a->confirmation_request_count;
	}
	return !any_bundle;
}

void nano::confirmation_solicitor::flush ()
{
	assert (prepared);
	for (auto const & request_queue : requests)
	{
		auto channel (request_queue.first);
		std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes_l;
		for (auto const & root_hash : request_queue.second)
		{
			roots_hashes_l.push_back (root_hash);
			if (roots_hashes_l.size () == nano::network::confirm_req_hashes_max)
			{
				nano::confirm_req req (roots_hashes_l);
				channel->send (req);
				roots_hashes_l.clear ();
			}
		}
		if (!roots_hashes_l.empty ())
		{
			nano::confirm_req req (roots_hashes_l);
			channel->send (req);
		}
	}
	prepared = false;
}
