#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>

nano::confirmation_solicitor::confirmation_solicitor (nano::node & node_a) :
node (node_a)
{
}

void nano::confirmation_solicitor::prepare ()
{
	assert (!prepared);
	requests.clear ();
	rebroadcasted = 0;
	representatives = node.rep_crawler.representatives ();
	prepared = true;
}

void nano::confirmation_solicitor::add (std::shared_ptr<nano::election> election_a)
{
	assert (prepared);
	if (election_a->confirmation_request_count % 8 == 1 && rebroadcasted++ < max_block_broadcasts)
	{
		node.network.flood_block (election_a->status.winner);
	}
	for (auto const & rep : representatives)
	{
		if (election_a->last_votes.find (rep.account) == election_a->last_votes.end ())
		{
			requests.insert ({ rep.channel, election_a });
		}
	}
	++election_a->confirmation_request_count;
}

void nano::confirmation_solicitor::flush ()
{
	assert (prepared);
	size_t batch_count = 0;
	size_t single_count = 0;
	for (auto i = requests.begin (), n (requests.end ()); i != n;)
	{
		if (batch_count++ < max_confirm_req_batches && i->channel->get_network_version () >= node.network_params.protocol.tcp_realtime_protocol_version_min)
		{
			auto channel = i->channel;
			std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes_l;
			while (i != n && i->channel == channel && roots_hashes_l.size () < nano::network::confirm_req_hashes_max)
			{
				roots_hashes_l.push_back (std::make_pair (i->election->status.winner->hash (), i->election->status.winner->root ()));
				++i;
			}
			nano::confirm_req req (roots_hashes_l);
			channel->send (req);
		}
		else if (single_count++ < max_confirm_req)
		{
			node.network.broadcast_confirm_req (i->election->status.winner);
			++i;
		}
	}
	prepared = false;
}
