#include <nano/node/active_transactions.hpp>

#include <nano/node/node.hpp>
#include <numeric>

size_t constexpr nano::active_transactions::max_broadcast_queue;

using namespace std::chrono;

nano::active_transactions::active_transactions (nano::node & node_a, bool delay_frontier_confirmation_height_updating) :
node (node_a),
multipliers_cb (20, 1.),
trended_active_difficulty (node.network_params.network.publish_threshold),
next_frontier_check (steady_clock::now () + (delay_frontier_confirmation_height_updating ? 60s : 0s)),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::request_loop);
	request_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

nano::active_transactions::~active_transactions ()
{
	stop ();
}

void nano::active_transactions::confirm_frontiers (nano::transaction const & transaction_a)
{
	// Limit maximum count of elections to start
	bool representative (node.config.enable_voting && node.wallets.reps_count > 0);
	/* Check less frequently for non-representative nodes
	~15 minutes for non-representative nodes, 3 minutes for representatives */
	int representative_factor = representative ? 3 * 60 : 15 * 60;
	// Decrease check time for test network
	int test_network_factor = node.network_params.network.is_test_network () ? 1000 : 1;
	if (std::chrono::steady_clock::now () >= next_frontier_check)
	{
		size_t max_elections (max_broadcast_queue / 4);
		size_t elections_count (0);
		for (auto i (node.store.latest_begin (transaction_a, next_frontier_account)), n (node.store.latest_end ()); i != n && !stopped && elections_count < max_elections; ++i)
		{
			nano::account_info info (i->second);
			if (info.block_count != info.confirmation_height)
			{
				auto block (node.store.block_get (transaction_a, info.head));
				if (!start (block))
				{
					++elections_count;
					// Calculate votes for local representatives
					if (representative)
					{
						node.block_processor.generator.add (block->hash ());
					}
				}
				// Update next account
				next_frontier_account = i->first.number () + 1;
			}
		}
		// 4 times slower check if all frontiers were confirmed
		int fully_confirmed_factor = (elections_count <= max_elections) ? 4 : 1;
		// Calculate next check time
		next_frontier_check = steady_clock::now () + seconds ((representative_factor * fully_confirmed_factor) / test_network_factor);
		// Set next account to 0 if all frontiers were confirmed
		next_frontier_account = (elections_count <= max_elections) ? 0 : next_frontier_account;
	}
}

void nano::active_transactions::request_confirm (std::unique_lock<std::mutex> & lock_a)
{
	std::unordered_set<nano::qualified_root> inactive;
	auto transaction (node.store.tx_begin_read ());
	unsigned unconfirmed_count (0);
	unsigned unconfirmed_announcements (0);
	unsigned could_fit_delay = node.network_params.network.is_test_network () ? announcement_long - 1 : 1;
	std::unordered_map<std::shared_ptr<nano::transport::channel>, std::vector<std::pair<nano::block_hash, nano::block_hash>>> requests_bundle;
	std::deque<std::shared_ptr<nano::block>> rebroadcast_bundle;
	std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>>>> confirm_req_bundle;

	auto roots_size (roots.size ());
	for (auto i (roots.get<1> ().begin ()), n (roots.get<1> ().end ()); i != n; ++i)
	{
		auto root (i->root);
		auto election_l (i->election);
		if ((election_l->confirmed || election_l->stopped) && election_l->announcements >= announcement_min - 1)
		{
			if (election_l->confirmed)
			{
				confirmed.push_back (election_l->status);
				if (confirmed.size () > election_history_size)
				{
					confirmed.pop_front ();
				}
			}
			inactive.insert (root);
		}
		else
		{
			if (election_l->announcements > announcement_long)
			{
				++unconfirmed_count;
				unconfirmed_announcements += election_l->announcements;
				// Log votes for very long unconfirmed elections
				if (election_l->announcements % 50 == 1)
				{
					auto tally_l (election_l->tally (transaction));
					election_l->log_votes (tally_l);
				}
				/* Escalation for long unconfirmed elections
				Start new elections for previous block & source
				if there are less than 100 active elections */
				if (election_l->announcements % announcement_long == 1 && roots_size < 100 && !node.network_params.network.is_test_network ())
				{
					std::shared_ptr<nano::block> previous;
					auto previous_hash (election_l->status.winner->previous ());
					if (!previous_hash.is_zero ())
					{
						previous = node.store.block_get (transaction, previous_hash);
						if (previous != nullptr)
						{
							add (std::move (previous));
						}
					}
					/* If previous block not existing/not commited yet, block_source can cause segfault for state blocks
					So source check can be done only if previous != nullptr or previous is 0 (open account) */
					if (previous_hash.is_zero () || previous != nullptr)
					{
						auto source_hash (node.ledger.block_source (transaction, *election_l->status.winner));
						if (!source_hash.is_zero ())
						{
							auto source (node.store.block_get (transaction, source_hash));
							if (source != nullptr)
							{
								add (std::move (source));
							}
						}
					}
					election_l->update_dependent ();
				}
			}
			if (election_l->announcements < announcement_long || election_l->announcements % announcement_long == could_fit_delay)
			{
				if (node.ledger.could_fit (transaction, *election_l->status.winner))
				{
					// Broadcast winner
					if (rebroadcast_bundle.size () < max_broadcast_queue)
					{
						rebroadcast_bundle.push_back (election_l->status.winner);
					}
				}
				else
				{
					if (election_l->announcements != 0)
					{
						election_l->stop ();
					}
				}
			}
			if (election_l->announcements % 4 == 1)
			{
				auto rep_channels (std::make_shared<std::vector<std::shared_ptr<nano::transport::channel>>> ());
				auto reps (node.rep_crawler.representatives (std::numeric_limits<size_t>::max ()));

				// Add all rep endpoints that haven't already voted. We use a set since multiple
				// reps may exist on an endpoint.
				std::unordered_set<std::shared_ptr<nano::transport::channel>> channels;
				for (auto & rep : reps)
				{
					if (election_l->last_votes.find (rep.account) == election_l->last_votes.end ())
					{
						channels.insert (rep.channel);

						if (node.config.logging.vote_logging ())
						{
							node.logger.try_log ("Representative did not respond to confirm_req, retrying: ", rep.account.to_account ());
						}
					}
				}

				rep_channels->insert (rep_channels->end (), channels.begin (), channels.end ());

				if ((!rep_channels->empty () && node.rep_crawler.total_weight () > node.config.online_weight_minimum.number ()) || roots_size > 5)
				{
					// broadcast_confirm_req_base modifies reps, so we clone it once to avoid aliasing
					if (node.network_params.network.is_live_network ())
					{
						if (confirm_req_bundle.size () < max_broadcast_queue)
						{
							confirm_req_bundle.push_back (std::make_pair (election_l->status.winner, rep_channels));
						}
					}
					else
					{
						for (auto & rep : *rep_channels)
						{
							auto rep_request (requests_bundle.find (rep));
							auto block (election_l->status.winner);
							auto root_hash (std::make_pair (block->hash (), block->root ()));
							if (rep_request == requests_bundle.end ())
							{
								if (requests_bundle.size () < max_broadcast_queue)
								{
									std::vector<std::pair<nano::block_hash, nano::block_hash>> insert_vector = { root_hash };
									requests_bundle.insert (std::make_pair (rep, insert_vector));
								}
							}
							else if (rep_request->second.size () < max_broadcast_queue * nano::network::confirm_req_hashes_max)
							{
								rep_request->second.push_back (root_hash);
							}
						}
					}
				}
				else
				{
					if (node.network_params.network.is_live_network ())
					{
						auto deque_l (node.network.udp_channels.random_set (100));
						auto vec (std::make_shared<std::vector<std::shared_ptr<nano::transport::channel>>> ());
						for (auto i : deque_l)
						{
							vec->push_back (i);
						}
						confirm_req_bundle.push_back (std::make_pair (election_l->status.winner, vec));
					}
					else
					{
						for (auto & rep : *rep_channels)
						{
							auto rep_request (requests_bundle.find (rep));
							auto block (election_l->status.winner);
							auto root_hash (std::make_pair (block->hash (), block->root ()));
							if (rep_request == requests_bundle.end ())
							{
								std::vector<std::pair<nano::block_hash, nano::block_hash>> insert_vector = { root_hash };
								requests_bundle.insert (std::make_pair (rep, insert_vector));
							}
							else
							{
								rep_request->second.push_back (root_hash);
							}
						}
					}
				}
			}
		}
		++election_l->announcements;
	}
	lock_a.unlock ();
	// Rebroadcast unconfirmed blocks
	if (!rebroadcast_bundle.empty ())
	{
		node.network.flood_block_batch (rebroadcast_bundle);
	}
	// Batch confirmation request
	if (!node.network_params.network.is_live_network () && !requests_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (requests_bundle, 50);
	}
	//confirm_req broadcast
	if (!confirm_req_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (confirm_req_bundle);
	}
	// Confirm frontiers when there aren't many confirmations already pending
	if (node.pending_confirmation_height.size () < confirmed_frontiers_max_pending_cut_off)
	{
		confirm_frontiers (transaction);
	}
	lock_a.lock ();
	// Erase inactive elections
	for (auto i (inactive.begin ()), n (inactive.end ()); i != n; ++i)
	{
		auto root_it (roots.find (*i));
		assert (root_it != roots.end ());
		for (auto & block : root_it->election->blocks)
		{
			auto erased (blocks.erase (block.first));
			(void)erased;
			assert (erased == 1);
		}
		for (auto & dependent_block : root_it->election->dependent_blocks)
		{
			adjust_difficulty (dependent_block);
		}
		roots.erase (*i);
	}
	if (unconfirmed_count > 0)
	{
		node.logger.try_log (boost::str (boost::format ("%1% blocks have been unconfirmed averaging %2% announcements") % unconfirmed_count % (unconfirmed_announcements / unconfirmed_count)));
	}
}

void nano::active_transactions::request_loop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	started = true;

	lock.unlock ();
	condition.notify_all ();
	lock.lock ();

	while (!stopped)
	{
		request_confirm (lock);
		update_active_difficulty (lock);

		// This prevents unnecessary waiting if stopped is set in-between the above check and now
		if (stopped)
		{
			break;
		}

		const auto extra_delay (std::min (roots.size (), max_broadcast_queue) * node.network.broadcast_interval_ms * 2);
		condition.wait_for (lock, std::chrono::milliseconds (node.network_params.network.request_interval_ms + extra_delay));
	}
}

void nano::active_transactions::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	lock.lock ();
	roots.clear ();
}

bool nano::active_transactions::start (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return add (block_a, confirmation_action_a);
}

bool nano::active_transactions::add (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	auto error (true);
	if (!stopped)
	{
		auto root (block_a->qualified_root ());
		auto existing (roots.find (root));
		if (existing == roots.end ())
		{
			auto election (std::make_shared<nano::election> (node, block_a, confirmation_action_a));
			uint64_t difficulty (0);
			auto error (nano::work_validate (*block_a, &difficulty));
			release_assert (!error);
			roots.insert (nano::conflict_info{ root, difficulty, difficulty, election });
			blocks.insert (std::make_pair (block_a->hash (), election));
			adjust_difficulty (block_a->hash ());
		}
		error = existing != roots.end ();
	}
	return error;
}

// Validate a vote and apply it to the current election if one exists
bool nano::active_transactions::vote (std::shared_ptr<nano::vote> vote_a, bool single_lock)
{
	std::shared_ptr<nano::election> election;
	bool replay (false);
	bool processed (false);
	{
		std::unique_lock<std::mutex> lock;
		if (!single_lock)
		{
			lock = std::unique_lock<std::mutex> (mutex);
		}
		for (auto vote_block : vote_a->blocks)
		{
			nano::election_vote_result result;
			if (vote_block.which ())
			{
				auto block_hash (boost::get<nano::block_hash> (vote_block));
				auto existing (blocks.find (block_hash));
				if (existing != blocks.end ())
				{
					result = existing->second->vote (vote_a->account, vote_a->sequence, block_hash);
				}
			}
			else
			{
				auto block (boost::get<std::shared_ptr<nano::block>> (vote_block));
				auto existing (roots.find (block->qualified_root ()));
				if (existing != roots.end ())
				{
					result = existing->election->vote (vote_a->account, vote_a->sequence, block->hash ());
				}
			}
			replay = replay || result.replay;
			processed = processed || result.processed;
		}
	}
	if (processed)
	{
		node.network.flood_vote (vote_a);
	}
	return replay;
}

bool nano::active_transactions::active (nano::qualified_root const & root_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.find (root_a) != roots.end ();
}

bool nano::active_transactions::active (nano::block const & block_a)
{
	return active (block_a.qualified_root ());
}

void nano::active_transactions::update_difficulty (nano::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a.qualified_root ()));
	if (existing != roots.end ())
	{
		uint64_t difficulty;
		auto error (nano::work_validate (block_a, &difficulty));
		assert (!error);
		if (difficulty > existing->difficulty)
		{
			roots.modify (existing, [difficulty](nano::conflict_info & info_a) {
				info_a.difficulty = difficulty;
			});
			adjust_difficulty (block_a.hash ());
		}
	}
}

void nano::active_transactions::adjust_difficulty (nano::block_hash const & hash_a)
{
	assert (!mutex.try_lock ());
	std::deque<std::pair<nano::block_hash, int64_t>> remaining_blocks;
	remaining_blocks.emplace_back (hash_a, 0);
	std::unordered_set<nano::block_hash> processed_blocks;
	std::vector<std::pair<nano::qualified_root, int64_t>> elections_list;
	double sum (0.);
	while (!remaining_blocks.empty ())
	{
		auto const & item (remaining_blocks.front ());
		auto hash (item.first);
		auto level (item.second);
		if (processed_blocks.find (hash) == processed_blocks.end ())
		{
			auto existing (blocks.find (hash));
			if (existing != blocks.end () && !existing->second->confirmed && !existing->second->stopped && existing->second->status.winner->hash () == hash)
			{
				auto previous (existing->second->status.winner->previous ());
				if (!previous.is_zero ())
				{
					remaining_blocks.emplace_back (previous, level + 1);
				}
				auto source (existing->second->status.winner->source ());
				if (!source.is_zero () && source != previous)
				{
					remaining_blocks.emplace_back (source, level + 1);
				}
				auto link (existing->second->status.winner->link ());
				if (!link.is_zero () && !node.ledger.is_epoch_link (link) && link != previous)
				{
					remaining_blocks.emplace_back (link, level + 1);
				}
				for (auto & dependent_block : existing->second->dependent_blocks)
				{
					remaining_blocks.emplace_back (dependent_block, level - 1);
				}
				processed_blocks.insert (hash);
				nano::qualified_root root (previous, existing->second->status.winner->root ());
				auto existing_root (roots.find (root));
				if (existing_root != roots.end ())
				{
					sum += nano::multiplier_from_difficulty (existing_root->difficulty, node.network_params.network.publish_threshold);
					elections_list.emplace_back (root, level);
				}
			}
		}
		remaining_blocks.pop_front ();
	}
	if (!elections_list.empty ())
	{
		double average = sum / elections_list.size ();

		// Set adjusted difficulty
		for (auto & item : elections_list)
		{
			auto existing_root (roots.find (item.first));
			uint64_t difficulty_a = nano::difficulty_from_multiplier (average + item.second, node.network_params.network.publish_threshold);
			roots.modify (existing_root, [difficulty_a](nano::conflict_info & info_a) {
				info_a.adjusted_difficulty = difficulty_a;
			});
		}
	}
}

void nano::active_transactions::update_active_difficulty (std::unique_lock<std::mutex> & lock_a)
{
	assert (lock_a.mutex () == &mutex && lock_a.owns_lock ());
	double multiplier (1.);
	if (!roots.empty ())
	{
		uint64_t max = roots.get<1> ().begin ()->adjusted_difficulty;
		assert (max >= node.network_params.network.publish_threshold);
		uint64_t min = (--roots.get<1> ().end ())->adjusted_difficulty;
		assert (min >= node.network_params.network.publish_threshold);

		multiplier = 0.5 * (nano::multiplier_from_difficulty (min, node.network_params.network.publish_threshold) + nano::multiplier_from_difficulty (max, node.network_params.network.publish_threshold));
	}
	assert (multiplier >= 1);
	multipliers_cb.push_front (multiplier);
	auto sum (std::accumulate (multipliers_cb.begin (), multipliers_cb.end (), double(0)));
	auto difficulty = nano::difficulty_from_multiplier (sum / multipliers_cb.size (), node.network_params.network.publish_threshold);
	assert (difficulty >= node.network_params.network.publish_threshold);
	trended_active_difficulty = difficulty;
}

uint64_t nano::active_transactions::active_difficulty ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return trended_active_difficulty;
}

// List of active blocks in elections
std::deque<std::shared_ptr<nano::block>> nano::active_transactions::list_blocks (bool single_lock)
{
	std::deque<std::shared_ptr<nano::block>> result;
	std::unique_lock<std::mutex> lock;
	if (!single_lock)
	{
		lock = std::unique_lock<std::mutex> (mutex);
	}
	for (auto i (roots.begin ()), n (roots.end ()); i != n; ++i)
	{
		result.push_back (i->election->status.winner);
	}
	return result;
}

std::deque<nano::election_status> nano::active_transactions::list_confirmed ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return confirmed;
}

void nano::active_transactions::erase (nano::block const & block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	if (roots.find (block_a.qualified_root ()) != roots.end ())
	{
		roots.erase (block_a.qualified_root ());
		node.logger.try_log (boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ()));
	}
}

bool nano::active_transactions::empty ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.empty ();
}

size_t nano::active_transactions::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return roots.size ();
}

bool nano::active_transactions::publish (std::shared_ptr<nano::block> block_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a->qualified_root ()));
	auto result (true);
	if (existing != roots.end ())
	{
		result = existing->election->publish (block_a);
		if (!result)
		{
			blocks.insert (std::make_pair (block_a->hash (), existing->election));
		}
	}
	return result;
}

void nano::active_transactions::confirm_block (nano::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (blocks.find (hash_a));
	if (existing != blocks.end () && !existing->second->confirmed && !existing->second->stopped && existing->second->status.winner->hash () == hash_a)
	{
		existing->second->confirm_once ();
	}
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name)
{
	size_t roots_count = 0;
	size_t blocks_count = 0;
	size_t confirmed_count = 0;

	{
		std::lock_guard<std::mutex> guard (active_transactions.mutex);
		roots_count = active_transactions.roots.size ();
		blocks_count = active_transactions.blocks.size ();
		confirmed_count = active_transactions.confirmed.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "roots", roots_count, sizeof (decltype (active_transactions.roots)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", blocks_count, sizeof (decltype (active_transactions.blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "confirmed", confirmed_count, sizeof (decltype (active_transactions.confirmed)::value_type) }));
	return composite;
}
}
