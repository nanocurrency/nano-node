#include <nano/node/active_transactions.hpp>
#include <nano/node/node.hpp>

#include <boost/pool/pool_alloc.hpp>

#include <numeric>

size_t constexpr nano::active_transactions::max_broadcast_queue;

using namespace std::chrono;

nano::active_transactions::active_transactions (nano::node & node_a) :
node (node_a),
multipliers_cb (20, 1.),
trended_active_difficulty (node.network_params.network.publish_threshold),
next_frontier_check (steady_clock::now ()),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::request_loop);
	request_loop ();
})
{
	nano::unique_lock<std::mutex> lock (mutex);
	condition.wait (lock, [& started = started] { return started; });
}

nano::active_transactions::~active_transactions ()
{
	stop ();
}

void nano::active_transactions::confirm_frontiers (nano::transaction const & transaction_a)
{
	// Limit maximum count of elections to start
	bool representative (node.config.enable_voting && node.wallets.reps_count > 0);
	bool half_princpal_representative (representative && node.wallets.half_principal_reps_count > 0);
	/* Check less frequently for regular nodes in auto mode */
	bool agressive_mode (half_princpal_representative || node.config.frontiers_confirmation == nano::frontiers_confirmation_mode::always);
	auto agressive_factor = agressive_mode ? 3min : 15min;
	// Decrease check time for test network
	auto is_test_network = node.network_params.network.is_test_network ();
	int test_network_factor = is_test_network ? 1000 : 1;
	auto roots_size = size ();
	auto max_elections = (max_broadcast_queue / 4);
	nano::unique_lock<std::mutex> lk (mutex);
	auto check_time_exceeded = std::chrono::steady_clock::now () >= next_frontier_check;
	lk.unlock ();
	auto low_active_elections = roots_size < max_elections;
	bool wallets_check_required = (!skip_wallets || !priority_wallet_cementable_frontiers.empty ()) && !agressive_mode;
	// To minimise dropping real-time transactions, set the maximum number of elections
	// for cementing frontiers to half the total active election maximum.
	const auto max_active = node.config.active_elections_size / 2;
	if (roots_size <= max_active && (check_time_exceeded || wallets_check_required || (!is_test_network && low_active_elections && agressive_mode)))
	{
		// When the number of active elections is low increase max number of elections for setting confirmation height.
		if (max_active > roots_size + max_elections)
		{
			max_elections = max_active - roots_size;
		}

		// Spend time prioritizing accounts to reduce voting traffic
		auto time_spent_prioritizing_ledger_accounts = (std::chrono::seconds (2));
		auto time_spent_prioritizing_wallet_accounts = std::chrono::milliseconds (50);
		prioritize_frontiers_for_confirmation (transaction_a, is_test_network ? std::chrono::milliseconds (50) : time_spent_prioritizing_ledger_accounts, time_spent_prioritizing_wallet_accounts);

		size_t elections_count (0);
		lk.lock ();
		auto start_elections_for_prioritized_frontiers = [&transaction_a, &elections_count, max_elections, &lk, &representative, this](prioritize_num_uncemented & cementable_frontiers) {
			while (!cementable_frontiers.empty () && !this->stopped && elections_count < max_elections)
			{
				auto cementable_account_front_it = cementable_frontiers.get<1> ().begin ();
				auto cementable_account = *cementable_account_front_it;
				cementable_frontiers.get<1> ().erase (cementable_account_front_it);
				lk.unlock ();
				nano::account_info info;
				auto error = node.store.account_get (transaction_a, cementable_account.account, info);
				if (!error)
				{
					uint64_t confirmation_height;
					error = node.store.confirmation_height_get (transaction_a, cementable_account.account, confirmation_height);
					release_assert (!error);

					if (info.block_count > confirmation_height && !this->node.pending_confirmation_height.is_processing_block (info.head))
					{
						auto block (this->node.store.block_get (transaction_a, info.head));
						if (!this->start (block))
						{
							++elections_count;
							// Calculate votes for local representatives
							if (representative)
							{
								this->node.block_processor.generator.add (block->hash ());
							}
						}
					}
				}
				lk.lock ();
			}
		};
		start_elections_for_prioritized_frontiers (priority_cementable_frontiers);
		start_elections_for_prioritized_frontiers (priority_wallet_cementable_frontiers);
		next_frontier_check = steady_clock::now () + (agressive_factor / test_network_factor);
	}
}
void nano::active_transactions::post_confirmation_height_set (nano::transaction const & transaction_a, std::shared_ptr<nano::block> block_a, nano::block_sideband const & sideband_a, nano::election_status_type election_status_type_a)
{
	if (election_status_type_a == nano::election_status_type::inactive_confirmation_height)
	{
		nano::account account (0);
		nano::uint128_t amount (0);
		bool is_state_send (false);
		nano::account pending_account (0);
		node.process_confirmed_data (transaction_a, block_a, block_a->hash (), sideband_a, account, amount, is_state_send, pending_account);
		node.observers.blocks.notify (nano::election_status{ block_a, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), nano::election_status_type::inactive_confirmation_height }, account, amount, is_state_send);
	}
	else
	{
		auto hash (block_a->hash ());
		nano::unique_lock<std::mutex> lock (mutex);
		auto existing (blocks.find (hash));
		if (existing != blocks.end ())
		{
			auto election = existing->second;
			if (election->confirmed && !election->stopped && election->status.winner->hash () == hash)
			{
				add_confirmed (existing->second->status, block_a->qualified_root ());

				node.receive_confirmed (transaction_a, block_a, hash);
				nano::account account (0);
				nano::uint128_t amount (0);
				bool is_state_send (false);
				nano::account pending_account (0);
				node.process_confirmed_data (transaction_a, block_a, hash, sideband_a, account, amount, is_state_send, pending_account);
				election->status.type = election_status_type_a;
				node.observers.blocks.notify (election->status, account, amount, is_state_send);
				if (amount > 0)
				{
					node.observers.account_balance.notify (account, false);
					if (!pending_account.is_zero ())
					{
						node.observers.account_balance.notify (pending_account, true);
					}
				}

				election->clear_blocks ();
				election->clear_dependent ();
			}
		}
	}
}

void nano::active_transactions::request_confirm (nano::unique_lock<std::mutex> & lock_a)
{
	std::unordered_set<nano::qualified_root> inactive;
	auto transaction (node.store.tx_begin_read ());
	unsigned unconfirmed_count (0);
	unsigned unconfirmed_request_count (0);
	unsigned could_fit_delay = node.network_params.network.is_test_network () ? high_confirmation_request_count - 1 : 1;
	std::unordered_map<std::shared_ptr<nano::transport::channel>, std::deque<std::pair<nano::block_hash, nano::block_hash>>> requests_bundle;
	std::deque<std::shared_ptr<nano::block>> rebroadcast_bundle;
	std::deque<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<std::vector<std::shared_ptr<nano::transport::channel>>>>> confirm_req_bundle;

	/* Confirm frontiers when there aren't many confirmations already pending and node finished initial bootstrap
	In auto mode start confirm only if node contains almost principal representative (half of required for principal weight) */
	lock_a.unlock ();
	if (node.config.frontiers_confirmation != nano::frontiers_confirmation_mode::disabled && node.ledger.block_count_cache > node.ledger.cemented_count + roots.size () && node.pending_confirmation_height.size () < confirmed_frontiers_max_pending_cut_off && node.ledger.block_count_cache >= node.ledger.bootstrap_weight_max_blocks)
	{
		confirm_frontiers (transaction);
	}
	lock_a.lock ();
	auto roots_size (roots.size ());
	for (auto i (roots.get<1> ().begin ()), n (roots.get<1> ().end ()); i != n; ++i)
	{
		auto root (i->root);
		auto election_l (i->election);
		if ((election_l->confirmed || election_l->stopped) && election_l->confirmation_request_count >= minimum_confirmation_request_count - 1)
		{
			if (election_l->stopped)
			{
				inactive.insert (root);
			}
		}
		else
		{
			if (election_l->confirmation_request_count > high_confirmation_request_count)
			{
				++unconfirmed_count;
				unconfirmed_request_count += election_l->confirmation_request_count;
				// Log votes for very long unconfirmed elections
				if (election_l->confirmation_request_count % 50 == 1)
				{
					auto tally_l (election_l->tally ());
					election_l->log_votes (tally_l);
				}
				/* Escalation for long unconfirmed elections
				Start new elections for previous block & source
				if there are less than 100 active elections */
				if (election_l->confirmation_request_count % high_confirmation_request_count == 1 && roots_size < 100 && !node.network_params.network.is_test_network ())
				{
					bool escalated (false);
					std::shared_ptr<nano::block> previous;
					auto previous_hash (election_l->status.winner->previous ());
					if (!previous_hash.is_zero ())
					{
						previous = node.store.block_get (transaction, previous_hash);
						if (previous != nullptr && blocks.find (previous_hash) == blocks.end () && !node.block_confirmed_or_being_confirmed (transaction, previous_hash))
						{
							add (std::move (previous));
							escalated = true;
						}
					}
					/* If previous block not existing/not commited yet, block_source can cause segfault for state blocks
					So source check can be done only if previous != nullptr or previous is 0 (open account) */
					if (previous_hash.is_zero () || previous != nullptr)
					{
						auto source_hash (node.ledger.block_source (transaction, *election_l->status.winner));
						if (!source_hash.is_zero () && source_hash != previous_hash && blocks.find (source_hash) == blocks.end ())
						{
							auto source (node.store.block_get (transaction, source_hash));
							if (source != nullptr && !node.block_confirmed_or_being_confirmed (transaction, source_hash))
							{
								add (std::move (source));
								escalated = true;
							}
						}
					}
					if (escalated)
					{
						election_l->update_dependent ();
					}
				}
			}
			if (election_l->confirmation_request_count < high_confirmation_request_count || election_l->confirmation_request_count % high_confirmation_request_count == could_fit_delay)
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
					if (election_l->confirmation_request_count != 0)
					{
						election_l->stop ();
						inactive.insert (root);
					}
				}
			}
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

			bool low_reps_weight (rep_channels->empty () || node.rep_crawler.total_weight () < node.config.online_weight_minimum.number ());
			if (low_reps_weight && roots_size <= 5 && !node.network_params.network.is_test_network ())
			{
				// Spam mode
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
				auto single_confirm_req_channels (std::make_shared<std::vector<std::shared_ptr<nano::transport::channel>>> ());
				for (auto & rep : *rep_channels)
				{
					if (rep->get_network_version () >= node.network_params.protocol.tcp_realtime_protocol_version_min)
					{
						// Send batch request to peers supporting confirm_req by hash + root
						auto rep_request (requests_bundle.find (rep));
						auto block (election_l->status.winner);
						auto root_hash (std::make_pair (block->hash (), block->root ()));
						if (rep_request == requests_bundle.end ())
						{
							if (requests_bundle.size () < max_broadcast_queue)
							{
								std::deque<std::pair<nano::block_hash, nano::block_hash>> insert_root_hash = { root_hash };
								requests_bundle.insert (std::make_pair (rep, insert_root_hash));
							}
						}
						else if (rep_request->second.size () < max_broadcast_queue * nano::network::confirm_req_hashes_max)
						{
							rep_request->second.push_back (root_hash);
						}
					}
					else
					{
						single_confirm_req_channels->push_back (rep);
					}
				}
				// broadcast_confirm_req_base modifies reps, so we clone it once to avoid aliasing
				if (confirm_req_bundle.size () < max_broadcast_queue && !single_confirm_req_channels->empty ())
				{
					confirm_req_bundle.push_back (std::make_pair (election_l->status.winner, single_confirm_req_channels));
				}
			}
		}
		++election_l->confirmation_request_count;
	}
	lock_a.unlock ();
	// Rebroadcast unconfirmed blocks
	if (!rebroadcast_bundle.empty ())
	{
		node.network.flood_block_batch (std::move (rebroadcast_bundle));
	}
	// Batch confirmation request
	if (!requests_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (requests_bundle, 50);
	}
	//confirm_req broadcast
	if (!confirm_req_bundle.empty ())
	{
		node.network.broadcast_confirm_req_batch (confirm_req_bundle);
	}
	lock_a.lock ();
	// Erase inactive elections
	for (auto i (inactive.begin ()), n (inactive.end ()); i != n; ++i)
	{
		auto root_it (roots.find (*i));
		if (root_it != roots.end ())
		{
			root_it->election->clear_blocks ();
			root_it->election->clear_dependent ();
			roots.erase (root_it);
		}
	}
	long_unconfirmed_size = unconfirmed_count;
	if (unconfirmed_count > 0)
	{
		node.logger.try_log (boost::str (boost::format ("%1% blocks have been unconfirmed averaging %2% confirmation requests") % unconfirmed_count % (unconfirmed_request_count / unconfirmed_count)));
	}
}

void nano::active_transactions::request_loop ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();

	// The wallets and active_transactions objects are mutually dependent, so we need a fully
	// constructed node before proceeding.
	this->node.node_initialized_latch.wait ();

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
		const auto wakeup (std::chrono::steady_clock::now () + std::chrono::milliseconds (node.network_params.network.request_interval_ms + extra_delay));
		// clang-format off
		condition.wait_until (lock, wakeup, [&wakeup, &stopped = stopped] { return stopped || std::chrono::steady_clock::now () >= wakeup; });
		// clang-format on
	}
}

void nano::active_transactions::prioritize_account_for_confirmation (nano::active_transactions::prioritize_num_uncemented & cementable_frontiers_a, size_t & cementable_frontiers_size_a, nano::account const & account_a, nano::account_info const & info_a, uint64_t confirmation_height)
{
	if (info_a.block_count > confirmation_height && !node.pending_confirmation_height.is_processing_block (info_a.head))
	{
		auto num_uncemented = info_a.block_count - confirmation_height;
		nano::lock_guard<std::mutex> guard (mutex);
		auto it = cementable_frontiers_a.find (account_a);
		if (it != cementable_frontiers_a.end ())
		{
			if (it->blocks_uncemented != num_uncemented)
			{
				// Account already exists and there is now a different uncemented block count so update it in the container
				cementable_frontiers_a.modify (it, [num_uncemented](nano::cementable_account & info) {
					info.blocks_uncemented = num_uncemented;
				});
			}
		}
		else
		{
			assert (cementable_frontiers_size_a <= max_priority_cementable_frontiers);
			if (cementable_frontiers_size_a == max_priority_cementable_frontiers)
			{
				// The maximum amount of frontiers stored has been reached. Check if the current frontier
				// has more uncemented blocks than the lowest uncemented frontier in the collection if so replace it.
				auto least_uncemented_frontier_it = cementable_frontiers_a.get<1> ().end ();
				--least_uncemented_frontier_it;
				if (num_uncemented > least_uncemented_frontier_it->blocks_uncemented)
				{
					cementable_frontiers_a.get<1> ().erase (least_uncemented_frontier_it);
					cementable_frontiers_a.emplace (account_a, num_uncemented);
				}
			}
			else
			{
				cementable_frontiers_a.emplace (account_a, num_uncemented);
			}
		}
		cementable_frontiers_size_a = cementable_frontiers_a.size ();
	}
}

void nano::active_transactions::prioritize_frontiers_for_confirmation (nano::transaction const & transaction_a, std::chrono::milliseconds ledger_accounts_time_a, std::chrono::milliseconds wallet_account_time_a)
{
	// Don't try to prioritize when there are a large number of pending confirmation heights as blocks can be cemented in the meantime, making the prioritization less reliable
	if (node.pending_confirmation_height.size () < confirmed_frontiers_max_pending_cut_off)
	{
		size_t priority_cementable_frontiers_size;
		size_t priority_wallet_cementable_frontiers_size;
		{
			nano::lock_guard<std::mutex> guard (mutex);
			priority_cementable_frontiers_size = priority_cementable_frontiers.size ();
			priority_wallet_cementable_frontiers_size = priority_wallet_cementable_frontiers.size ();
		}
		nano::timer<std::chrono::milliseconds> wallet_account_timer;
		wallet_account_timer.start ();

		if (!skip_wallets)
		{
			// Prioritize wallet accounts first
			{
				nano::lock_guard<std::mutex> lock (node.wallets.mutex);
				auto wallet_transaction (node.wallets.tx_begin_read ());
				auto const & items = node.wallets.items;
				if (items.empty ())
				{
					skip_wallets = true;
				}
				for (auto item_it = items.cbegin (); item_it != items.cend (); ++item_it)
				{
					// Skip this wallet if it has been traversed already while there are others still awaiting
					if (wallet_accounts_already_iterated.find (item_it->first) != wallet_accounts_already_iterated.end ())
					{
						continue;
					}

					nano::account_info info;
					auto & wallet (item_it->second);
					nano::lock_guard<std::recursive_mutex> wallet_lock (wallet->store.mutex);

					auto & next_wallet_frontier_account = next_wallet_frontier_accounts.emplace (item_it->first, wallet_store::special_count).first->second;

					auto i (wallet->store.begin (wallet_transaction, next_wallet_frontier_account));
					auto n (wallet->store.end ());
					uint64_t confirmation_height = 0;
					for (; i != n; ++i)
					{
						auto & account (i->first);
						if (!node.store.account_get (transaction_a, account, info) && !node.store.confirmation_height_get (transaction_a, account, confirmation_height))
						{
							// If it exists in normal priority collection delete from there.
							auto it = priority_cementable_frontiers.find (account);
							if (it != priority_cementable_frontiers.end ())
							{
								nano::lock_guard<std::mutex> guard (mutex);
								priority_cementable_frontiers.erase (it);
								priority_cementable_frontiers_size = priority_cementable_frontiers.size ();
							}

							prioritize_account_for_confirmation (priority_wallet_cementable_frontiers, priority_wallet_cementable_frontiers_size, account, info, confirmation_height);

							if (wallet_account_timer.since_start () >= wallet_account_time_a)
							{
								break;
							}
						}
						next_wallet_frontier_account = account.number () + 1;
					}
					// Go back to the beginning when we have reached the end of the wallet accounts for this wallet
					if (i == n)
					{
						wallet_accounts_already_iterated.emplace (item_it->first);
						next_wallet_frontier_accounts.at (item_it->first) = wallet_store::special_count;

						// Skip wallet accounts when they have all been traversed
						if (std::next (item_it) == items.cend ())
						{
							wallet_accounts_already_iterated.clear ();
							skip_wallets = true;
						}
					}
				}
			}
		}

		nano::timer<std::chrono::milliseconds> timer;
		timer.start ();

		auto i (node.store.latest_begin (transaction_a, next_frontier_account));
		auto n (node.store.latest_end ());
		uint64_t confirmation_height = 0;
		for (; i != n && !stopped; ++i)
		{
			auto const & account (i->first);
			auto const & info (i->second);
			if (priority_wallet_cementable_frontiers.find (account) == priority_wallet_cementable_frontiers.end ())
			{
				if (!node.store.confirmation_height_get (transaction_a, account, confirmation_height))
				{
					prioritize_account_for_confirmation (priority_cementable_frontiers, priority_cementable_frontiers_size, account, info, confirmation_height);
				}
			}
			next_frontier_account = account.number () + 1;
			if (timer.since_start () >= ledger_accounts_time_a)
			{
				break;
			}
		}

		// Go back to the beginning when we have reached the end of the accounts and start with wallet accounts next time
		if (i == n)
		{
			next_frontier_account = 0;
			skip_wallets = false;
		}
	}
}

void nano::active_transactions::stop ()
{
	nano::unique_lock<std::mutex> lock (mutex);
	if (!started)
	{
		condition.wait (lock, [& started = started] { return started; });
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
	nano::lock_guard<std::mutex> lock (mutex);
	return add (block_a, confirmation_action_a);
}

bool nano::active_transactions::add (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	auto error (true);
	if (!stopped)
	{
		auto root (block_a->qualified_root ());
		auto existing (roots.find (root));
		if (existing == roots.end () && confirmed_set.get<1> ().find (root) == confirmed_set.get<1> ().end ())
		{
			auto hash (block_a->hash ());
			auto election (nano::make_shared<nano::election> (node, block_a, confirmation_action_a));
			uint64_t difficulty (0);
			error = nano::work_validate (*block_a, &difficulty);
			release_assert (!error);
			roots.insert (nano::conflict_info{ root, difficulty, difficulty, election });
			blocks.insert (std::make_pair (hash, election));
			adjust_difficulty (hash);
			election->insert_inactive_votes_cache ();
		}
		if (roots.size () >= node.config.active_elections_size)
		{
			flush_lowest ();
		}
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
		nano::unique_lock<std::mutex> lock;
		if (!single_lock)
		{
			lock = nano::unique_lock<std::mutex> (mutex);
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
				else
				{
					add_inactive_votes_cache (block_hash, vote_a->account);
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
				else
				{
					add_inactive_votes_cache (block->hash (), vote_a->account);
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
	nano::lock_guard<std::mutex> lock (mutex);
	return roots.find (root_a) != roots.end ();
}

bool nano::active_transactions::active (nano::block const & block_a)
{
	return active (block_a.qualified_root ());
}

void nano::active_transactions::update_difficulty (nano::block const & block_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a.qualified_root ()));
	if (existing != roots.end ())
	{
		uint64_t difficulty;
		auto error (nano::work_validate (block_a, &difficulty));
		(void)error;
		assert (!error);
		if (difficulty > existing->difficulty)
		{
			if (node.config.logging.active_update_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Block %1% was updated from difficulty %2% to %3%") % block_a.hash ().to_string () % nano::to_string_hex (existing->difficulty) % nano::to_string_hex (difficulty)));
			}
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
	int64_t highest_level (0);
	int64_t lowest_level (0);
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
					sum += nano::difficulty::to_multiplier (existing_root->difficulty, node.network_params.network.publish_threshold);
					elections_list.emplace_back (root, level);
					if (level > highest_level)
					{
						highest_level = level;
					}
					else if (level < lowest_level)
					{
						lowest_level = level;
					}
				}
			}
		}
		remaining_blocks.pop_front ();
	}
	if (!elections_list.empty ())
	{
		double multiplier = sum / elections_list.size ();
		uint64_t average = nano::difficulty::from_multiplier (multiplier, node.network_params.network.publish_threshold);
		// Prevent overflow
		int64_t limiter (0);
		if (std::numeric_limits<std::uint64_t>::max () - average < static_cast<uint64_t> (highest_level))
		{
			// Highest adjusted difficulty value should be std::numeric_limits<std::uint64_t>::max ()
			limiter = std::numeric_limits<std::uint64_t>::max () - average + highest_level;
			assert (std::numeric_limits<std::uint64_t>::max () == average + highest_level - limiter);
		}
		else if (average < std::numeric_limits<std::uint64_t>::min () - lowest_level)
		{
			// Lowest adjusted difficulty value should be std::numeric_limits<std::uint64_t>::min ()
			limiter = std::numeric_limits<std::uint64_t>::min () - average + lowest_level;
			assert (std::numeric_limits<std::uint64_t>::min () == average + lowest_level - limiter);
		}

		// Set adjusted difficulty
		for (auto & item : elections_list)
		{
			auto existing_root (roots.find (item.first));
			uint64_t difficulty_a = average + item.second - limiter;
			roots.modify (existing_root, [difficulty_a](nano::conflict_info & info_a) {
				info_a.adjusted_difficulty = difficulty_a;
			});
		}
	}
}

void nano::active_transactions::update_active_difficulty (nano::unique_lock<std::mutex> & lock_a)
{
	assert (lock_a.mutex () == &mutex && lock_a.owns_lock ());
	double multiplier (1.);
	if (!roots.empty ())
	{
		std::vector<uint64_t> active_root_difficulties;
		active_root_difficulties.reserve (roots.size ());
		auto min_election_time (std::chrono::milliseconds (node.network_params.network.request_interval_ms));
		auto cutoff (std::chrono::steady_clock::now () - min_election_time);
		for (auto & root : roots)
		{
			if (!root.election->confirmed && !root.election->stopped && root.election->election_start < cutoff)
			{
				active_root_difficulties.push_back (root.adjusted_difficulty);
			}
		}
		if (!active_root_difficulties.empty ())
		{
			multiplier = nano::difficulty::to_multiplier (active_root_difficulties[active_root_difficulties.size () / 2], node.network_params.network.publish_threshold);
		}
	}
	assert (multiplier >= 1);
	multipliers_cb.push_front (multiplier);
	auto sum (std::accumulate (multipliers_cb.begin (), multipliers_cb.end (), double(0)));
	auto difficulty = nano::difficulty::from_multiplier (sum / multipliers_cb.size (), node.network_params.network.publish_threshold);
	assert (difficulty >= node.network_params.network.publish_threshold);

	trended_active_difficulty = difficulty;
	node.observers.difficulty.notify (trended_active_difficulty);
}

uint64_t nano::active_transactions::active_difficulty ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return trended_active_difficulty;
}

uint64_t nano::active_transactions::limited_active_difficulty ()
{
	return std::min (active_difficulty (), node.config.max_work_generate_difficulty);
}

// List of active blocks in elections
std::deque<std::shared_ptr<nano::block>> nano::active_transactions::list_blocks (bool single_lock)
{
	std::deque<std::shared_ptr<nano::block>> result;
	nano::unique_lock<std::mutex> lock;
	if (!single_lock)
	{
		lock = nano::unique_lock<std::mutex> (mutex);
	}
	for (auto i (roots.begin ()), n (roots.end ()); i != n; ++i)
	{
		result.push_back (i->election->status.winner);
	}
	return result;
}

std::deque<nano::election_status> nano::active_transactions::list_confirmed ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return confirmed;
}

void nano::active_transactions::add_confirmed (nano::election_status const & status_a, nano::qualified_root const & root_a)
{
	confirmed.push_back (status_a);
	auto inserted (confirmed_set.insert (nano::confirmed_set_info{ std::chrono::steady_clock::now (), root_a }));
	if (confirmed.size () > node.config.confirmation_history_size)
	{
		confirmed.pop_front ();
		if (inserted.second)
		{
			confirmed_set.erase (confirmed_set.begin ());
		}
	}
}

void nano::active_transactions::erase (nano::block const & block_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	auto root_it (roots.find (block_a.qualified_root ()));
	if (root_it != roots.end ())
	{
		root_it->election->stop ();
		root_it->election->clear_blocks ();
		root_it->election->clear_dependent ();
		roots.erase (root_it);
		node.logger.try_log (boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ()));
	}
}

void nano::active_transactions::flush_lowest ()
{
	size_t count (0);
	assert (!roots.empty ());
	auto & sorted_roots = roots.get<1> ();
	for (auto it = sorted_roots.rbegin (); it != sorted_roots.rend ();)
	{
		if (count != 2)
		{
			auto election = it->election;
			if (election->confirmation_request_count > high_confirmation_request_count && !election->confirmed && !election->stopped && !node.wallets.watcher->is_watched (it->root))
			{
				it = decltype (it){ sorted_roots.erase (std::next (it).base ()) };
				election->stop ();
				election->clear_blocks ();
				election->clear_dependent ();
				count++;
			}
			else
			{
				++it;
			}
		}
		else
		{
			break;
		}
	}
}

bool nano::active_transactions::empty ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return roots.empty ();
}

size_t nano::active_transactions::size ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return roots.size ();
}

bool nano::active_transactions::publish (std::shared_ptr<nano::block> block_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	auto existing (roots.find (block_a->qualified_root ()));
	auto result (true);
	if (existing != roots.end ())
	{
		auto election (existing->election);
		result = election->publish (block_a);
		if (!result && !election->confirmed)
		{
			blocks.insert (std::make_pair (block_a->hash (), election));
		}
	}
	return result;
}

void nano::active_transactions::clear_block (nano::block_hash const & hash_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	auto existing (blocks.find (hash_a));
	if (existing != blocks.end ())
	{
		auto election = existing->second;
		election->clear_blocks ();
		election->clear_dependent ();
	}
}

// Returns the type of election status requiring callbacks calling later
boost::optional<nano::election_status_type> nano::active_transactions::confirm_block (nano::transaction const & transaction_a, std::shared_ptr<nano::block> block_a, nano::block_sideband const & sideband_a)
{
	auto hash (block_a->hash ());
	nano::unique_lock<std::mutex> lock (mutex);
	auto existing (blocks.find (hash));
	if (existing != blocks.end ())
	{
		if (!existing->second->confirmed && !existing->second->stopped && existing->second->status.winner->hash () == hash)
		{
			existing->second->confirm_once (nano::election_status_type::active_confirmation_height);
			return nano::election_status_type::active_confirmation_height;
		}
		else
		{
			return boost::optional<nano::election_status_type>{};
		}
	}
	else
	{
		return nano::election_status_type::inactive_confirmation_height;
	}
}

size_t nano::active_transactions::priority_cementable_frontiers_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return priority_cementable_frontiers.size ();
}

size_t nano::active_transactions::priority_wallet_cementable_frontiers_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return priority_wallet_cementable_frontiers.size ();
}

boost::circular_buffer<double> nano::active_transactions::difficulty_trend ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return multipliers_cb;
}

size_t nano::active_transactions::inactive_votes_cache_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return inactive_votes_cache.size ();
}

void nano::active_transactions::add_inactive_votes_cache (nano::block_hash const & hash_a, nano::account const & representative_a)
{
	// Check principal representative status
	if (node.ledger.weight (representative_a) > node.minimum_principal_weight ())
	{
		auto existing (inactive_votes_cache.get<1> ().find (hash_a));
		if (existing != inactive_votes_cache.get<1> ().end ())
		{
			auto is_new (false);
			inactive_votes_cache.get<1> ().modify (existing, [representative_a, &is_new](nano::gap_information & info) {
				auto it = std::find (info.voters.begin (), info.voters.end (), representative_a);
				is_new = (it == info.voters.end ());
				if (is_new)
				{
					info.arrival = std::chrono::steady_clock::now ();
					info.voters.push_back (representative_a);
				}
			});

			if (is_new)
			{
				node.gap_cache.bootstrap_check (existing->voters, hash_a);
			}
		}
		else
		{
			inactive_votes_cache.insert ({ std::chrono::steady_clock::now (), hash_a, std::vector<nano::account> (1, representative_a) });
			if (inactive_votes_cache.size () > inactive_votes_cache_max)
			{
				inactive_votes_cache.get<0> ().erase (inactive_votes_cache.get<0> ().begin ());
			}
		}
	}
}

nano::gap_information nano::active_transactions::find_inactive_votes_cache (nano::block_hash const & hash_a)
{
	auto existing (inactive_votes_cache.get<1> ().find (hash_a));
	if (existing != inactive_votes_cache.get<1> ().end ())
	{
		return *existing;
	}
	else
	{
		return nano::gap_information{ std::chrono::steady_clock::time_point{}, 0, std::vector<nano::account>{} };
	}
}

nano::cementable_account::cementable_account (nano::account const & account_a, size_t blocks_uncemented_a) :
account (account_a), blocks_uncemented (blocks_uncemented_a)
{
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name)
{
	size_t roots_count = 0;
	size_t blocks_count = 0;
	size_t confirmed_count = 0;

	{
		nano::lock_guard<std::mutex> guard (active_transactions.mutex);
		roots_count = active_transactions.roots.size ();
		blocks_count = active_transactions.blocks.size ();
		confirmed_count = active_transactions.confirmed.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "roots", roots_count, sizeof (decltype (active_transactions.roots)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", blocks_count, sizeof (decltype (active_transactions.blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "confirmed", confirmed_count, sizeof (decltype (active_transactions.confirmed)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "priority_wallet_cementable_frontiers_count", active_transactions.priority_wallet_cementable_frontiers_size (), sizeof (nano::cementable_account) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "priority_cementable_frontiers_count", active_transactions.priority_cementable_frontiers_size (), sizeof (nano::cementable_account) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "inactive_votes_cache_count", active_transactions.inactive_votes_cache_size (), sizeof (nano::gap_information) }));
	return composite;
}
}
