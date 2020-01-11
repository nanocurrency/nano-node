#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>

using namespace std::chrono_literals;

nano::confirmation_solicitor::confirmation_solicitor (nano::network & network_a, nano::network_constants const & params_a) :
max_confirm_req_batches (params_a.is_test_network () ? 1 : 20),
max_block_broadcasts (params_a.is_test_network () ? 4 : 30),
network (network_a),
min_time_between_requests (params_a.is_test_network () ? 10ms : 3s),
min_time_between_floods (params_a.is_test_network () ? 20ms : 6s),
min_request_count_flood (params_a.is_test_network () ? 0 : 4)
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
	auto const now (std::chrono::steady_clock::now ());
	auto const flood_cutoff (now - min_time_between_floods);
	if (election_a->confirmation_request_count >= min_request_count_flood && election_a->last_broadcast < flood_cutoff && rebroadcasted++ < max_block_broadcasts)
	{
		election_a->last_broadcast = now;
		network.flood_block (election_a->status.winner);
	}
	auto const max_channel_requests (max_confirm_req_batches * nano::network::confirm_req_hashes_max);
	auto const request_cutoff (now - min_time_between_requests);
	auto failure (true);
	if (election_a->last_request < request_cutoff)
	{
		for (auto const & rep : representatives)
		{
			if (election_a->last_votes.find (rep.account) == election_a->last_votes.end ())
			{
				auto & request_queue (requests[rep.channel]);
				if (request_queue.size () < max_channel_requests)
				{
					request_queue.emplace_back (election_a->status.winner->hash (), election_a->status.winner->root ());
					++election_a->confirmation_request_count;
					election_a->last_request = now;
					failure = false;
				}
			}
		}
	}
	return failure;
}

void nano::confirmation_solicitor::flush ()
{
	assert (prepared);
	for (auto const & request_queue : requests)
	{
		auto const & channel (request_queue.first);
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
