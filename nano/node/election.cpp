#include <nano/node/election.hpp>
#include <nano/node/node.hpp>

nano::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

nano::election::election (nano::node & node_a, std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a) :
confirmation_action (confirmation_action_a),
node (node_a),
election_start (std::chrono::steady_clock::now ()),
status ({ block_a, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero () }),
confirmed (false),
stopped (false),
announcements (0)
{
	last_votes.insert (std::make_pair (node.network_params.random.not_an_account, nano::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () }));
	blocks.insert (std::make_pair (block_a->hash (), block_a));
	update_dependent ();
	node.observers.active.notify (status, nano::election_observer_type::added);
}

void nano::election::compute_rep_votes (nano::transaction const & transaction_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &transaction_a](nano::public_key const & pub_a, nano::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, status.winner));
			this->node.vote_processor.vote (vote, std::make_shared<nano::transport::channel_udp> (this->node.network.udp_channels, this->node.network.endpoint ()));
		});
	}
}

void nano::election::confirm_once (nano::election_observer_type type_a)
{
	if (!confirmed.exchange (true))
	{
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		auto winner_l (status.winner);
		auto node_l (node.shared ());
		auto confirmation_action_l (confirmation_action);
		node.background ([node_l, winner_l, confirmation_action_l]() {
			node_l->process_confirmed (winner_l);
			confirmation_action_l (winner_l);
		});
		if (announcements > node_l->active.announcement_long)
		{
			--node_l->active.long_unconfirmed_size;
		}
		observers_result (type_a);
	}
}

void nano::election::stop ()
{
	if (!stopped && !confirmed)
	{
		stopped = true;
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		observers_result (nano::election_observer_type::stopped);
	}
}

bool nano::election::have_quorum (nano::tally_t const & tally_a, nano::uint128_t tally_sum) const
{
	bool result = false;
	if (tally_sum >= node.config.online_weight_minimum.number ())
	{
		auto i (tally_a.begin ());
		auto const & first (i->first);
		++i;
		auto second (i != tally_a.end () ? i->first : 0);
		auto delta_l (node.delta ());
		result = tally_a.begin ()->first > (second + delta_l);
	}
	return result;
}

nano::tally_t nano::election::tally (nano::transaction const & transaction_a)
{
	std::unordered_map<nano::block_hash, nano::uint128_t> block_weights;
	for (auto vote_info : last_votes)
	{
		block_weights[vote_info.second.hash] += node.ledger.weight (transaction_a, vote_info.first);
	}
	last_tally = block_weights;
	nano::tally_t result;
	for (auto item : block_weights)
	{
		auto block (blocks.find (item.first));
		if (block != blocks.end ())
		{
			result.insert (std::make_pair (item.second, block->second));
		}
	}
	return result;
}

void nano::election::confirm_if_quorum (nano::transaction const & transaction_a)
{
	auto tally_l (tally (transaction_a));
	assert (!tally_l.empty ());
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	status.tally = winner->first;
	nano::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.config.online_weight_minimum.number () && block_l->hash () != status.winner->hash ())
	{
		auto node_l (node.shared ());
		node_l->block_processor.force (block_l);
		status.winner = block_l;
		update_dependent ();
		node_l->active.adjust_difficulty (block_l->hash ());
	}
	if (have_quorum (tally_l, sum))
	{
		if (node.config.logging.vote_logging () || blocks.size () > 1)
		{
			log_votes (tally_l);
		}
		confirm_once ();
	}
}

void nano::election::log_votes (nano::tally_t const & tally_a) const
{
	std::stringstream tally;
	tally << boost::str (boost::format ("\nVote tally for root %1%") % status.winner->root ().to_string ());
	for (auto i (tally_a.begin ()), n (tally_a.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\nBlock %1% weight %2%") % i->second->hash ().to_string () % i->first.convert_to<std::string> ());
	}
	for (auto i (last_votes.begin ()), n (last_votes.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\n%1% %2%") % i->first.to_account () % i->second.hash.to_string ());
	}
	node.logger.try_log (tally.str ());
}

nano::election_vote_result nano::election::vote (nano::account rep, uint64_t sequence, nano::block_hash block_hash)
{
	// see republish_vote documentation for an explanation of these rules
	auto transaction (node.store.tx_begin_read ());
	auto replay (false);
	auto supply (node.online_reps.online_stake ());
	auto weight (node.ledger.weight (transaction, rep));
	auto should_process (false);
	if (node.network_params.network.is_test_network () || weight > supply / 1000) // 0.1% or above
	{
		unsigned int cooldown;
		if (weight < supply / 100) // 0.1% to 1%
		{
			cooldown = 15;
		}
		else if (weight < supply / 20) // 1% to 5%
		{
			cooldown = 5;
		}
		else // 5% or above
		{
			cooldown = 1;
		}
		auto last_vote_it (last_votes.find (rep));
		if (last_vote_it == last_votes.end ())
		{
			should_process = true;
		}
		else
		{
			auto last_vote (last_vote_it->second);
			if (last_vote.sequence < sequence || (last_vote.sequence == sequence && last_vote.hash < block_hash))
			{
				if (last_vote.time <= std::chrono::steady_clock::now () - std::chrono::seconds (cooldown))
				{
					should_process = true;
				}
			}
			else
			{
				replay = true;
			}
		}
		if (should_process)
		{
			last_votes[rep] = { std::chrono::steady_clock::now (), sequence, block_hash };
			if (!confirmed)
			{
				confirm_if_quorum (transaction);
			}
		}
	}
	return nano::election_vote_result (replay, should_process);
}

bool nano::election::publish (std::shared_ptr<nano::block> block_a)
{
	auto result (false);
	if (blocks.size () >= 10)
	{
		if (last_tally[block_a->hash ()] < node.online_reps.online_stake () / 10)
		{
			result = true;
		}
	}
	if (!result)
	{
		auto transaction (node.store.tx_begin_read ());
		result = node.validate_block_by_previous (transaction, block_a);
		if (!result)
		{
			if (blocks.find (block_a->hash ()) == blocks.end ())
			{
				blocks.insert (std::make_pair (block_a->hash (), block_a));
				node.observers.active.notify (nano::election_status{ block_a, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start) }, nano::election_observer_type::added);
				confirm_if_quorum (transaction);
				node.network.flood_block (block_a);
			}
		}
	}
	return result;
}

size_t nano::election::last_votes_size ()
{
	std::lock_guard<std::mutex> lock (node.active.mutex);
	return last_votes.size ();
}

void nano::election::update_dependent ()
{
	assert (!node.active.mutex.try_lock ());
	std::vector<nano::block_hash> blocks_search;
	auto hash (status.winner->hash ());
	auto previous (status.winner->previous ());
	if (!previous.is_zero ())
	{
		blocks_search.push_back (previous);
	}
	auto source (status.winner->source ());
	if (!source.is_zero () && source != previous)
	{
		blocks_search.push_back (source);
	}
	auto link (status.winner->link ());
	if (!link.is_zero () && !node.ledger.is_epoch_link (link) && link != previous)
	{
		blocks_search.push_back (link);
	}
	for (auto & block_search : blocks_search)
	{
		auto existing (node.active.blocks.find (block_search));
		if (existing != node.active.blocks.end () && !existing->second->confirmed && !existing->second->stopped)
		{
			if (existing->second->dependent_blocks.find (hash) == existing->second->dependent_blocks.end ())
			{
				existing->second->dependent_blocks.insert (hash);
			}
		}
	}
}

void nano::election::observers_result (nano::election_observer_type type_a)
{
	// Elections observer
	node.observers.active.notify (status, type_a);
	if (blocks.size () > 1)
	{
		auto type_blocks (type_a == nano::election_observer_type::stopped ? nano::election_observer_type::stopped: nano::election_observer_type::forked);
		for (auto & item : last_tally)
		{
			if (item.first != status.winner->hash ())
			{
				auto block (blocks.find (item.first));
				if (block != blocks.end ())
				{
					node.observers.active.notify (nano::election_status{ block->second, item.second, status.election_end, status.election_duration }, type_blocks);
				}
			}
		}
	}
}
