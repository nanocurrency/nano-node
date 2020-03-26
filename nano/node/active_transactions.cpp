#include <nano/lib/threading.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/confirmation_height_processor.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/secure/blockstore.hpp>

#include <boost/format.hpp>
#include <boost/variant/get.hpp>

#include <numeric>

using namespace std::chrono;

nano::active_transactions::active_transactions (nano::node & node_a, nano::confirmation_height_processor & confirmation_height_processor_a) :
confirmation_height_processor (confirmation_height_processor_a),
node (node_a),
multipliers_cb (20, 1.),
trended_active_difficulty (node_a.network_params.network.publish_thresholds.epoch_1),
check_all_elections_period (node_a.network_params.network.is_test_network () ? 10ms : 5s),
election_time_to_live (node_a.network_params.network.is_test_network () ? 0s : 2s),
prioritized_cutoff (std::max<size_t> (1, node_a.config.active_elections_size / 10)),
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::request_loop);
	request_loop ();
})
{
	// Register a callback which will get called after a block is cemented
	confirmation_height_processor.add_cemented_observer ([this](std::shared_ptr<nano::block> callback_block_a) {
		this->block_cemented_callback (callback_block_a);
	});

	// Register a callback which will get called after a batch of blocks is written and observer calls finished
	confirmation_height_processor.add_block_already_cemented_observer ([this](nano::block_hash const & hash_a) {
		this->block_already_cemented_callback (hash_a);
	});

	nano::unique_lock<std::mutex> lock (mutex);
	condition.wait (lock, [& started = started] { return started; });
}

nano::active_transactions::~active_transactions ()
{
	stop ();
}

void nano::active_transactions::search_frontiers (nano::transaction const & transaction_a)
{
	// Limit maximum count of elections to start
	auto rep_counts (node.wallets.rep_counts ());
	bool representative (node.config.enable_voting && rep_counts.voting > 0);
	bool half_princpal_representative (representative && rep_counts.half_principal > 0);
	/* Check less frequently for regular nodes in auto mode */
	bool agressive_mode (half_princpal_representative || node.config.frontiers_confirmation == nano::frontiers_confirmation_mode::always);
	auto request_interval (std::chrono::milliseconds (node.network_params.network.request_interval_ms));
	auto agressive_factor = request_interval * (agressive_mode ? 20 : 100);
	// Decrease check time for test network
	auto is_test_network = node.network_params.network.is_test_network ();
	int test_network_factor = is_test_network ? 1000 : 1;
	auto roots_size = size ();
	nano::unique_lock<std::mutex> lk (mutex);
	auto check_time_exceeded = std::chrono::steady_clock::now () >= next_frontier_check;
	lk.unlock ();
	auto max_elections = 1000;
	auto low_active_elections = roots_size < max_elections;
	bool wallets_check_required = (!skip_wallets || !priority_wallet_cementable_frontiers.empty ()) && !agressive_mode;
	// Minimise dropping real-time transactions, set the number of frontiers added to a factor of the total number of active elections
	auto max_active = node.config.active_elections_size / 20;
	if (roots_size <= max_active && (check_time_exceeded || wallets_check_required || (!is_test_network && low_active_elections && agressive_mode)))
	{
		// When the number of active elections is low increase max number of elections for setting confirmation height.
		if (max_active > roots_size + max_elections)
		{
			max_elections = max_active - roots_size;
		}

		// Spend time prioritizing accounts to reduce voting traffic
		auto time_spent_prioritizing_ledger_accounts = request_interval / 10;
		auto time_spent_prioritizing_wallet_accounts = request_interval / 25;
		prioritize_frontiers_for_confirmation (transaction_a, is_test_network ? std::chrono::milliseconds (50) : time_spent_prioritizing_ledger_accounts, time_spent_prioritizing_wallet_accounts);

		size_t elections_count (0);
		lk.lock ();
		auto start_elections_for_prioritized_frontiers = [&transaction_a, &elections_count, max_elections, &lk, &representative, this](prioritize_num_uncemented & cementable_frontiers) {
			while (!cementable_frontiers.empty () && !this->stopped && elections_count < max_elections)
			{
				auto cementable_account_front_it = cementable_frontiers.get<tag_uncemented> ().begin ();
				auto cementable_account = *cementable_account_front_it;
				cementable_frontiers.get<tag_uncemented> ().erase (cementable_account_front_it);
				lk.unlock ();
				nano::account_info info;
				auto error = node.store.account_get (transaction_a, cementable_account.account, info);
				if (!error)
				{
					nano::confirmation_height_info confirmation_height_info;
					error = node.store.confirmation_height_get (transaction_a, cementable_account.account, confirmation_height_info);
					release_assert (!error);

					if (info.block_count > confirmation_height_info.height && !this->confirmation_height_processor.is_processing_block (info.head))
					{
						auto block (this->node.store.block_get (transaction_a, info.head));
						auto election = this->insert (block);
						if (election.inserted)
						{
							election.election->transition_active ();
							++elections_count;
							// Calculate votes for local representatives
							if (election.prioritized && representative)
							{
								this->node.block_processor.generator.add (info.head);
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

void nano::active_transactions::block_cemented_callback (std::shared_ptr<nano::block> const & block_a)
{
	auto transaction = node.store.tx_begin_read ();

	boost::optional<nano::election_status_type> election_status_type;
	if (!confirmation_height_processor.is_processing_block (block_a->hash ()))
	{
		election_status_type = confirm_block (transaction, block_a);
	}
	else
	{
		// This block was explicitly added to the confirmation height_processor
		election_status_type = nano::election_status_type::active_confirmed_quorum;
	}

	if (election_status_type.is_initialized ())
	{
		if (election_status_type == nano::election_status_type::inactive_confirmation_height)
		{
			nano::account account (0);
			nano::uint128_t amount (0);
			bool is_state_send (false);
			nano::account pending_account (0);
			node.process_confirmed_data (transaction, block_a, block_a->hash (), account, amount, is_state_send, pending_account);
			node.observers.blocks.notify (nano::election_status{ block_a, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), 0, 1, 0, nano::election_status_type::inactive_confirmation_height }, account, amount, is_state_send);
		}
		else
		{
			auto hash (block_a->hash ());
			nano::unique_lock<std::mutex> election_winners_lk (election_winner_details_mutex);
			auto existing (election_winner_details.find (hash));
			if (existing != election_winner_details.end ())
			{
				auto election = existing->second;
				election_winner_details.erase (hash);
				election_winners_lk.unlock ();
				nano::unique_lock<std::mutex> lk (mutex);
				if (election->confirmed () && election->status.winner->hash () == hash)
				{
					add_recently_cemented (election->status);
					lk.unlock ();
					node.receive_confirmed (transaction, block_a, hash);
					nano::account account (0);
					nano::uint128_t amount (0);
					bool is_state_send (false);
					nano::account pending_account (0);
					node.process_confirmed_data (transaction, block_a, hash, account, amount, is_state_send, pending_account);
					lk.lock ();
					election->status.type = *election_status_type;
					election->status.confirmation_request_count = election->confirmation_request_count;
					node.observers.blocks.notify (election->status, account, amount, is_state_send);
					lk.unlock ();
					if (amount > 0)
					{
						node.observers.account_balance.notify (account, false);
						if (!pending_account.is_zero ())
						{
							node.observers.account_balance.notify (pending_account, true);
						}
					}
				}
			}
		}
	}
}

void nano::active_transactions::add_election_winner_details (nano::block_hash const & hash_a, std::shared_ptr<nano::election> const & election_a)
{
	nano::lock_guard<std::mutex> guard (election_winner_details_mutex);
	election_winner_details.emplace (hash_a, election_a);
}

void nano::active_transactions::block_already_cemented_callback (nano::block_hash const & hash_a)
{
	// Depending on timing there is a situation where the election_winner_details is not reset.
	// This can happen when a block wins an election, and the block is confirmed + observer
	// called before the block hash gets added to election_winner_details. If the block is confirmed
	// callbacks have already been done, so we can safely just remove it.
	nano::lock_guard<std::mutex> guard (election_winner_details_mutex);
	election_winner_details.erase (hash_a);
}

void nano::active_transactions::request_confirm (nano::unique_lock<std::mutex> & lock_a)
{
	debug_assert (!mutex.try_lock ());
	/*
	 * Confirm frontiers when there aren't many confirmations already pending and node finished initial bootstrap
	 * In auto mode start confirm only if node contains almost principal representative (half of required for principal weight)
	 */

	// Due to the confirmation height processor working asynchronously and compressing several roots into one frontier, probably_unconfirmed_frontiers can be wrong
	{
		auto pending_confirmation_height_size (confirmation_height_processor.awaiting_processing_size ());
		bool probably_unconfirmed_frontiers (node.ledger.cache.block_count > node.ledger.cache.cemented_count + roots.size () + pending_confirmation_height_size);
		bool bootstrap_weight_reached (node.ledger.cache.block_count >= node.ledger.bootstrap_weight_max_blocks);
		if (node.config.frontiers_confirmation != nano::frontiers_confirmation_mode::disabled && bootstrap_weight_reached && probably_unconfirmed_frontiers && pending_confirmation_height_size < confirmed_frontiers_max_pending_cut_off)
		{
			lock_a.unlock ();
			search_frontiers (node.store.tx_begin_read ());
			lock_a.lock ();
			update_adjusted_difficulty (); // New roots sorting
		}
	}

	// Only representatives ready to receive batched confirm_req
	nano::confirmation_solicitor solicitor (node.network, node.network_params.network);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<size_t>::max (), node.network_params.protocol.tcp_realtime_protocol_version_min));

	bool const representative_l (node.config.enable_voting && node.wallets.rep_counts ().voting > 0);
	std::vector<nano::block_hash> hashes_generation_l;
	auto & sorted_roots_l (roots.get<tag_difficulty> ());
	auto const election_ttl_cutoff_l (std::chrono::steady_clock::now () - election_time_to_live);
	bool const check_all_elections_l (std::chrono::steady_clock::now () - last_check_all_elections > check_all_elections_period);
	size_t const this_loop_target_l (check_all_elections_l ? sorted_roots_l.size () : prioritized_cutoff);
	size_t unconfirmed_count_l (0);
	nano::timer<std::chrono::milliseconds> elapsed (nano::timer_state::started);

	/*
	 * Loop through active elections in descending order of proof-of-work difficulty, requesting confirmation
	 *
	 * Only up to a certain amount of elections are queued for confirmation request and block rebroadcasting. The remaining elections can still be confirmed if votes arrive
	 * Elections extending the soft config.active_elections_size limit are flushed after a certain time-to-live cutoff
	 * Flushed elections are later re-activated via frontier confirmation
	 */
	for (auto i = sorted_roots_l.begin (), n = sorted_roots_l.end (); i != n && unconfirmed_count_l < this_loop_target_l;)
	{
		auto & election_l (i->election);
		bool const confirmed_l (election_l->confirmed ());

		// Queue vote generation for newly prioritized elections
		if (!i->prioritized && unconfirmed_count_l < prioritized_cutoff)
		{
			sorted_roots_l.modify (i, [](nano::conflict_info & info_a) { info_a.prioritized = true; });
			if (representative_l && !confirmed_l)
			{
				hashes_generation_l.push_back (election_l->status.winner->hash ());
			}
		}

		unconfirmed_count_l += !confirmed_l;
		bool const overflow_l (unconfirmed_count_l > node.config.active_elections_size && election_l->election_start < election_ttl_cutoff_l && !node.wallets.watcher->is_watched (i->root));
		if (overflow_l || election_l->transition_time (solicitor))
		{
			election_l->cleanup ();
			i = sorted_roots_l.erase (i);
		}
		else
		{
			++i;
		}
	}
	lock_a.unlock ();
	solicitor.flush ();
	for (auto const & hash_l : hashes_generation_l)
	{
		node.block_processor.generator.add (hash_l);
	}
	lock_a.lock ();

	// This is updated after the loop to ensure slow machines don't do the full check often
	if (check_all_elections_l)
	{
		last_check_all_elections = std::chrono::steady_clock::now ();
		if (node.config.logging.timing_logging () && this_loop_target_l > prioritized_cutoff)
		{
			node.logger.try_log (boost::str (boost::format ("Processed %1% elections (%2% were already confirmed) in %3% %4%") % this_loop_target_l % (this_loop_target_l - unconfirmed_count_l) % elapsed.value ().count () % elapsed.unit ()));
		}
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

	while (!stopped && !node.flags.disable_request_loop)
	{
		// Account for the time spent in request_confirm by defining the wakeup point beforehand
		const auto wakeup_l (std::chrono::steady_clock::now () + std::chrono::milliseconds (node.network_params.network.request_interval_ms));

		update_adjusted_difficulty ();
		update_active_difficulty (lock);
		request_confirm (lock);

		// Sleep until all broadcasts are done, plus the remaining loop time
		if (!stopped)
		{
			condition.wait_until (lock, wakeup_l, [&wakeup_l, &stopped = stopped] { return stopped || std::chrono::steady_clock::now () >= wakeup_l; });
		}
	}
}

void nano::active_transactions::prioritize_account_for_confirmation (nano::active_transactions::prioritize_num_uncemented & cementable_frontiers_a, size_t & cementable_frontiers_size_a, nano::account const & account_a, nano::account_info const & info_a, uint64_t confirmation_height)
{
	if (info_a.block_count > confirmation_height && !confirmation_height_processor.is_processing_block (info_a.head))
	{
		auto num_uncemented = info_a.block_count - confirmation_height;
		nano::lock_guard<std::mutex> guard (mutex);
		auto it = cementable_frontiers_a.get<tag_account> ().find (account_a);
		if (it != cementable_frontiers_a.get<tag_account> ().end ())
		{
			if (it->blocks_uncemented != num_uncemented)
			{
				// Account already exists and there is now a different uncemented block count so update it in the container
				cementable_frontiers_a.get<tag_account> ().modify (it, [num_uncemented](nano::cementable_account & info) {
					info.blocks_uncemented = num_uncemented;
				});
			}
		}
		else
		{
			debug_assert (cementable_frontiers_size_a <= max_priority_cementable_frontiers);
			if (cementable_frontiers_size_a == max_priority_cementable_frontiers)
			{
				// The maximum amount of frontiers stored has been reached. Check if the current frontier
				// has more uncemented blocks than the lowest uncemented frontier in the collection if so replace it.
				auto least_uncemented_frontier_it = cementable_frontiers_a.get<tag_uncemented> ().end ();
				--least_uncemented_frontier_it;
				if (num_uncemented > least_uncemented_frontier_it->blocks_uncemented)
				{
					cementable_frontiers_a.get<tag_uncemented> ().erase (least_uncemented_frontier_it);
					cementable_frontiers_a.get<tag_account> ().emplace (account_a, num_uncemented);
				}
			}
			else
			{
				cementable_frontiers_a.get<tag_account> ().emplace (account_a, num_uncemented);
			}
		}
		cementable_frontiers_size_a = cementable_frontiers_a.size ();
	}
}

void nano::active_transactions::prioritize_frontiers_for_confirmation (nano::transaction const & transaction_a, std::chrono::milliseconds ledger_accounts_time_a, std::chrono::milliseconds wallet_account_time_a)
{
	// Don't try to prioritize when there are a large number of pending confirmation heights as blocks can be cemented in the meantime, making the prioritization less reliable
	if (confirmation_height_processor.awaiting_processing_size () < confirmed_frontiers_max_pending_cut_off)
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
					if (wallet_ids_already_iterated.find (item_it->first) != wallet_ids_already_iterated.end ())
					{
						continue;
					}

					nano::account_info info;
					auto & wallet (item_it->second);
					nano::lock_guard<std::recursive_mutex> wallet_lock (wallet->store.mutex);

					auto & next_wallet_frontier_account = next_wallet_id_accounts.emplace (item_it->first, wallet_store::special_count).first->second;

					auto i (wallet->store.begin (wallet_transaction, next_wallet_frontier_account));
					auto n (wallet->store.end ());
					nano::confirmation_height_info confirmation_height_info;
					for (; i != n; ++i)
					{
						auto const & account (i->first);
						if (!node.store.account_get (transaction_a, account, info) && !node.store.confirmation_height_get (transaction_a, account, confirmation_height_info))
						{
							// If it exists in normal priority collection delete from there.
							auto it = priority_cementable_frontiers.find (account);
							if (it != priority_cementable_frontiers.end ())
							{
								nano::lock_guard<std::mutex> guard (mutex);
								priority_cementable_frontiers.erase (it);
								priority_cementable_frontiers_size = priority_cementable_frontiers.size ();
							}

							prioritize_account_for_confirmation (priority_wallet_cementable_frontiers, priority_wallet_cementable_frontiers_size, account, info, confirmation_height_info.height);

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
						wallet_ids_already_iterated.emplace (item_it->first);
						next_wallet_id_accounts.at (item_it->first) = wallet_store::special_count;

						// Skip wallet accounts when they have all been traversed
						if (std::next (item_it) == items.cend ())
						{
							wallet_ids_already_iterated.clear ();
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
		nano::confirmation_height_info confirmation_height_info;
		for (; i != n && !stopped; ++i)
		{
			auto const & account (i->first);
			auto const & info (i->second);
			if (priority_wallet_cementable_frontiers.find (account) == priority_wallet_cementable_frontiers.end ())
			{
				if (!node.store.confirmation_height_get (transaction_a, account, confirmation_height_info))
				{
					prioritize_account_for_confirmation (priority_cementable_frontiers, priority_cementable_frontiers_size, account, info, confirmation_height_info.height);
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

nano::election_insertion_result nano::active_transactions::insert_impl (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	nano::election_insertion_result result;
	if (!stopped)
	{
		auto root (block_a->qualified_root ());
		auto existing (roots.get<tag_root> ().find (root));
		if (existing == roots.get<tag_root> ().end ())
		{
			if (recently_confirmed.get<tag_root> ().find (root) == recently_confirmed.get<tag_root> ().end ())
			{
				result.inserted = true;
				auto hash (block_a->hash ());
				result.election = nano::make_shared<nano::election> (node, block_a, confirmation_action_a);
				auto difficulty (block_a->difficulty ());
				result.prioritized = roots.size () < prioritized_cutoff || difficulty > last_prioritized_difficulty.value_or (0);
				double multiplier (normalized_difficulty (block_a));
				roots.get<tag_root> ().emplace (nano::conflict_info{ root, multiplier, difficulty, result.election, result.prioritized });
				blocks.emplace (hash, result.election);
				add_adjust_difficulty (hash);
				result.election->insert_inactive_votes_cache (hash);
			}
		}
		else
		{
			result.election = existing->election;
		}
	}
	return result;
}

nano::election_insertion_result nano::active_transactions::insert (std::shared_ptr<nano::block> block_a, std::function<void(std::shared_ptr<nano::block>)> const & confirmation_action_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	return insert_impl (block_a, confirmation_action_a);
}

// Validate a vote and apply it to the current election if one exists
nano::vote_code nano::active_transactions::vote (std::shared_ptr<nano::vote> vote_a)
{
	// If none of the hashes are active, votes are not republished
	bool at_least_one (false);
	// If all hashes were recently confirmed then it is a replay
	unsigned recently_confirmed_counter (0);
	bool replay (false);
	bool processed (false);
	{
		nano::lock_guard<std::mutex> lock (mutex);
		for (auto vote_block : vote_a->blocks)
		{
			nano::election_vote_result result;
			auto & recently_confirmed_by_hash (recently_confirmed.get<tag_hash> ());
			if (vote_block.which ())
			{
				auto block_hash (boost::get<nano::block_hash> (vote_block));
				auto existing (blocks.find (block_hash));
				if (existing != blocks.end ())
				{
					at_least_one = true;
					result = existing->second->vote (vote_a->account, vote_a->sequence, block_hash);
				}
				else if (recently_confirmed_by_hash.count (block_hash) == 0)
				{
					add_inactive_votes_cache (block_hash, vote_a->account);
				}
				else
				{
					++recently_confirmed_counter;
				}
			}
			else
			{
				auto block (boost::get<std::shared_ptr<nano::block>> (vote_block));
				auto existing (roots.get<tag_root> ().find (block->qualified_root ()));
				if (existing != roots.get<tag_root> ().end ())
				{
					at_least_one = true;
					result = existing->election->vote (vote_a->account, vote_a->sequence, block->hash ());
				}
				else if (recently_confirmed_by_hash.count (block->hash ()) == 0)
				{
					add_inactive_votes_cache (block->hash (), vote_a->account);
				}
				else
				{
					++recently_confirmed_counter;
				}
			}
			processed = processed || result.processed;
			replay = replay || result.replay;
		}
	}

	if (at_least_one)
	{
		// Republish vote if it is new and the node does not host a principal representative (or close to)
		if (processed && !node.wallets.rep_counts ().have_half_rep ())
		{
			node.network.flood_vote (vote_a, 0.5f);
		}
		return replay ? nano::vote_code::replay : nano::vote_code::vote;
	}
	else if (recently_confirmed_counter == vote_a->blocks.size ())
	{
		return nano::vote_code::replay;
	}
	else
	{
		return nano::vote_code::indeterminate;
	}
}

bool nano::active_transactions::active (nano::qualified_root const & root_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	return roots.get<tag_root> ().find (root_a) != roots.get<tag_root> ().end ();
}

bool nano::active_transactions::active (nano::block const & block_a)
{
	return active (block_a.qualified_root ());
}

std::shared_ptr<nano::election> nano::active_transactions::election (nano::qualified_root const & root_a) const
{
	std::shared_ptr<nano::election> result;
	nano::lock_guard<std::mutex> lock (mutex);
	auto existing = roots.get<tag_root> ().find (root_a);
	if (existing != roots.get<tag_root> ().end ())
	{
		result = existing->election;
	}
	return result;
}

void nano::active_transactions::update_difficulty (std::shared_ptr<nano::block> block_a)
{
	nano::unique_lock<std::mutex> lock (mutex);
	auto existing_election (roots.get<tag_root> ().find (block_a->qualified_root ()));
	if (existing_election != roots.get<tag_root> ().end ())
	{
		double multiplier (normalized_difficulty (block_a));
		if (multiplier > existing_election->multiplier)
		{
			if (node.config.logging.active_update_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Block %1% was updated from multiplier %2% to %3%") % block_a->hash ().to_string () % existing_election->multiplier % multiplier));
			}
			roots.get<tag_root> ().modify (existing_election, [multiplier](nano::conflict_info & info_a) {
				info_a.multiplier = multiplier;
			});
			existing_election->election->publish (block_a);
			add_adjust_difficulty (block_a->hash ());
		}
	}
}

double nano::active_transactions::normalized_difficulty (std::shared_ptr<nano::block> block_a)
{
	auto difficulty (block_a->difficulty ());
	uint64_t threshold (0);
	if (block_a->has_sideband ())
	{
		threshold = nano::work_threshold (block_a->work_version (), block_a->sideband ().details);
	}
	else
	{
		threshold = nano::work_threshold_base (block_a->work_version ());
	}
	double multiplier (nano::difficulty::to_multiplier (difficulty, threshold));
	debug_assert (multiplier >= 1);
	// Normalization rules
	if (threshold == node.network_params.network.publish_thresholds.epoch_1)
	{
		// Epoch 1
		auto rate (node.network_params.network.publish_thresholds.epoch_2 / node.network_params.network.publish_thresholds.epoch_1);
		auto rate_4 (rate * 4);
		if (multiplier < rate) // From 0.75 to 2.0
		{
			multiplier = (multiplier / rate) * 1.25 + 0.75;
		}
		else if (multiplier < rate_4) // From 2.0 to 4.0
		{
			multiplier = (multiplier / rate_4) * 2.0 + 2.0;
		}
		else // Same computational resources for epoch 1 & epoch 2
		{
			multiplier = multiplier / rate;
		}
	}
	else if (threshold == node.network_params.network.publish_thresholds.epoch_2_receive)
	{
		// Epoch 2 (receive / epoch subtypes)
		auto rate (node.network_params.network.publish_thresholds.epoch_2 / node.network_params.network.publish_thresholds.epoch_2_receive);
		auto rate_4 (rate * 4);
		if (multiplier < rate) // From 0.5 to 2.0
		{
			multiplier = (multiplier / rate) * 1.5 + 0.5;
		}
		else if (multiplier < rate_4) // From 2.0 to 4.0
		{
			multiplier = (multiplier / rate_4) * 2.0 + 2.0;
		}
		else // Same computational resources for send & receive
		{
			multiplier = multiplier / rate;
		}
	}
	return multiplier;
}

void nano::active_transactions::add_adjust_difficulty (nano::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	adjust_difficulty_list.push_back (hash_a);
}

void nano::active_transactions::update_adjusted_difficulty ()
{
	debug_assert (!mutex.try_lock ());
	std::unordered_set<nano::block_hash> processed_blocks;
	while (!adjust_difficulty_list.empty ())
	{
		auto const & adjust_difficulty_item (adjust_difficulty_list.front ());
		std::deque<std::pair<nano::block_hash, int64_t>> remaining_blocks;
		remaining_blocks.emplace_back (adjust_difficulty_item, 0);
		adjust_difficulty_list.pop_front ();
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
				if (existing != blocks.end () && !existing->second->confirmed () && existing->second->status.winner->hash () == hash)
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
					auto existing_root (roots.get<tag_root> ().find (root));
					if (existing_root != roots.get<tag_root> ().end ())
					{
						sum += existing_root->multiplier;
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
			uint64_t average = nano::difficulty::from_multiplier (multiplier, node.network_params.network.publish_thresholds.epoch_1);
			// Prevent overflow
			int64_t limiter (0);
			if (std::numeric_limits<std::uint64_t>::max () - average < static_cast<uint64_t> (highest_level))
			{
				// Highest adjusted difficulty value should be std::numeric_limits<std::uint64_t>::max ()
				limiter = std::numeric_limits<std::uint64_t>::max () - average + highest_level;
				debug_assert (std::numeric_limits<std::uint64_t>::max () == average + highest_level - limiter);
			}
			else if (average < std::numeric_limits<std::uint64_t>::min () - lowest_level)
			{
				// Lowest adjusted difficulty value should be std::numeric_limits<std::uint64_t>::min ()
				limiter = std::numeric_limits<std::uint64_t>::min () - average + lowest_level;
				debug_assert (std::numeric_limits<std::uint64_t>::min () == average + lowest_level - limiter);
			}

			// Set adjusted difficulty
			for (auto & item : elections_list)
			{
				auto existing_root (roots.get<tag_root> ().find (item.first));
				uint64_t difficulty_a = average + item.second - limiter;
				if (existing_root->adjusted_difficulty != difficulty_a)
				{
					roots.get<tag_root> ().modify (existing_root, [difficulty_a](nano::conflict_info & info_a) {
						info_a.adjusted_difficulty = difficulty_a;
					});
				}
			}
		}
	}
}

void nano::active_transactions::update_active_difficulty (nano::unique_lock<std::mutex> & lock_a)
{
	debug_assert (!mutex.try_lock ());
	last_prioritized_difficulty.reset ();
	double multiplier (1.);
	// Heurestic to filter out non-saturated network and frontier confirmation
	if (roots.size () > prioritized_cutoff / 2 || (node.network_params.network.is_test_network () && !roots.empty ()))
	{
		auto & sorted_roots = roots.get<tag_difficulty> ();
		std::vector<uint64_t> prioritized;
		prioritized.reserve (std::min (sorted_roots.size (), prioritized_cutoff));
		for (auto it (sorted_roots.begin ()), end (sorted_roots.end ()); it != end && prioritized.size () < prioritized_cutoff; ++it)
		{
			if (!it->election->confirmed ())
			{
				prioritized.push_back (it->adjusted_difficulty);
			}
		}
		if (prioritized.size () > 10 || (node.network_params.network.is_test_network () && !prioritized.empty ()))
		{
			multiplier = nano::difficulty::to_multiplier (prioritized[prioritized.size () / 2], node.network_params.network.publish_thresholds.epoch_1);
		}
		if (!prioritized.empty ())
		{
			last_prioritized_difficulty = prioritized.back ();
		}
	}
	debug_assert (multiplier >= nano::difficulty::to_multiplier (node.network_params.network.publish_thresholds.entry, node.network_params.network.publish_thresholds.epoch_1));
	multipliers_cb.push_front (multiplier);
	auto sum (std::accumulate (multipliers_cb.begin (), multipliers_cb.end (), double(0)));
	auto difficulty = nano::difficulty::from_multiplier (sum / multipliers_cb.size (), node.network_params.network.publish_thresholds.epoch_1);
	debug_assert (difficulty >= node.network_params.network.publish_thresholds.entry);

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
std::deque<std::shared_ptr<nano::block>> nano::active_transactions::list_blocks ()
{
	std::deque<std::shared_ptr<nano::block>> result;
	nano::lock_guard<std::mutex> lock (mutex);
	for (auto & root : roots)
	{
		result.push_back (root.election->status.winner);
	}
	return result;
}

std::deque<nano::election_status> nano::active_transactions::list_recently_cemented ()
{
	nano::lock_guard<std::mutex> lock (mutex);
	return recently_cemented;
}

void nano::active_transactions::add_recently_cemented (nano::election_status const & status_a)
{
	recently_cemented.push_back (status_a);
	if (recently_cemented.size () > node.config.confirmation_history_size)
	{
		recently_cemented.pop_front ();
	}
}

void nano::active_transactions::add_recently_confirmed (nano::qualified_root const & root_a, nano::block_hash const & hash_a)
{
	recently_confirmed.get<tag_sequence> ().emplace_back (root_a, hash_a);
	if (recently_confirmed.size () > recently_confirmed_size)
	{
		recently_confirmed.get<tag_sequence> ().pop_front ();
	}
}

void nano::active_transactions::erase (nano::block const & block_a)
{
	nano::lock_guard<std::mutex> lock (mutex);
	auto root_it (roots.get<tag_root> ().find (block_a.qualified_root ()));
	if (root_it != roots.get<tag_root> ().end ())
	{
		root_it->election->cleanup ();
		root_it->election->adjust_dependent_difficulty ();
		roots.get<tag_root> ().erase (root_it);
		node.logger.try_log (boost::str (boost::format ("Election erased for block block %1% root %2%") % block_a.hash ().to_string () % block_a.root ().to_string ()));
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
	auto existing (roots.get<tag_root> ().find (block_a->qualified_root ()));
	auto result (true);
	if (existing != roots.get<tag_root> ().end ())
	{
		auto election (existing->election);
		result = election->publish (block_a);
		if (!result)
		{
			blocks.emplace (block_a->hash (), election);
		}
	}
	return result;
}

// Returns the type of election status requiring callbacks calling later
boost::optional<nano::election_status_type> nano::active_transactions::confirm_block (nano::transaction const & transaction_a, std::shared_ptr<nano::block> block_a)
{
	auto hash (block_a->hash ());
	nano::unique_lock<std::mutex> lock (mutex);
	auto existing (blocks.find (hash));
	if (existing != blocks.end ())
	{
		if (!existing->second->confirmed () && existing->second->status.winner->hash () == hash)
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
		auto & inactive_by_hash (inactive_votes_cache.get<tag_hash> ());
		auto existing (inactive_by_hash.find (hash_a));
		if (existing != inactive_by_hash.end ())
		{
			if (!existing->confirmed || !existing->bootstrap_started)
			{
				auto is_new (false);
				inactive_by_hash.modify (existing, [representative_a, &is_new](nano::inactive_cache_information & info) {
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
					bool confirmed (false);
					if (inactive_votes_bootstrap_check (existing->voters, hash_a, confirmed) && !existing->bootstrap_started)
					{
						inactive_by_hash.modify (existing, [](nano::inactive_cache_information & info) {
							info.bootstrap_started = true;
						});
					}
					if (confirmed && !existing->confirmed)
					{
						inactive_by_hash.modify (existing, [](nano::inactive_cache_information & info) {
							info.confirmed = true;
						});
					}
				}
			}
		}
		else
		{
			std::vector<nano::account> representative_vector (1, representative_a);
			bool confirmed (false);
			bool start_bootstrap (inactive_votes_bootstrap_check (representative_vector, hash_a, confirmed));
			auto & inactive_by_arrival (inactive_votes_cache.get<tag_arrival> ());
			inactive_by_arrival.emplace (nano::inactive_cache_information{ std::chrono::steady_clock::now (), hash_a, representative_vector, start_bootstrap, confirmed });
			if (inactive_votes_cache.size () > node.flags.inactive_votes_cache_size)
			{
				inactive_by_arrival.erase (inactive_by_arrival.begin ());
			}
		}
	}
}

nano::inactive_cache_information nano::active_transactions::find_inactive_votes_cache (nano::block_hash const & hash_a)
{
	auto & inactive_by_hash (inactive_votes_cache.get<tag_hash> ());
	auto existing (inactive_by_hash.find (hash_a));
	if (existing != inactive_by_hash.end ())
	{
		return *existing;
	}
	else
	{
		return nano::inactive_cache_information{};
	}
}

void nano::active_transactions::erase_inactive_votes_cache (nano::block_hash const & hash_a)
{
	inactive_votes_cache.get<tag_hash> ().erase (hash_a);
}

bool nano::active_transactions::inactive_votes_bootstrap_check (std::vector<nano::account> const & voters_a, nano::block_hash const & hash_a, bool & confirmed_a)
{
	uint128_t tally;
	for (auto const & voter : voters_a)
	{
		tally += node.ledger.weight (voter);
	}
	bool start_bootstrap (false);
	if (tally >= node.config.online_weight_minimum.number ())
	{
		start_bootstrap = true;
		confirmed_a = true;
	}
	else if (!node.flags.disable_legacy_bootstrap && tally > node.gap_cache.bootstrap_threshold ())
	{
		start_bootstrap = true;
	}
	if (start_bootstrap && !node.ledger.block_exists (hash_a))
	{
		auto node_l (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + node.network_params.bootstrap.gap_cache_bootstrap_start_interval, [node_l, hash_a]() {
			auto transaction (node_l->store.tx_begin_read ());
			if (!node_l->store.block_exists (transaction, hash_a))
			{
				if (!node_l->bootstrap_initiator.in_progress ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Missing block %1% which has enough votes to warrant lazy bootstrapping it") % hash_a.to_string ()));
				}
				if (!node_l->flags.disable_lazy_bootstrap)
				{
					node_l->bootstrap_initiator.bootstrap_lazy (hash_a);
				}
				else if (!node_l->flags.disable_legacy_bootstrap)
				{
					node_l->bootstrap_initiator.bootstrap ();
				}
			}
		});
	}
	return start_bootstrap;
}

size_t nano::active_transactions::election_winner_details_size ()
{
	nano::lock_guard<std::mutex> guard (election_winner_details_mutex);
	return election_winner_details.size ();
}

nano::cementable_account::cementable_account (nano::account const & account_a, size_t blocks_uncemented_a) :
account (account_a), blocks_uncemented (blocks_uncemented_a)
{
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (active_transactions & active_transactions, const std::string & name)
{
	size_t roots_count;
	size_t blocks_count;
	size_t recently_confirmed_count;
	size_t recently_cemented_count;

	{
		nano::lock_guard<std::mutex> guard (active_transactions.mutex);
		roots_count = active_transactions.roots.size ();
		blocks_count = active_transactions.blocks.size ();
		recently_confirmed_count = active_transactions.recently_confirmed.size ();
		recently_cemented_count = active_transactions.recently_cemented.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "roots", roots_count, sizeof (decltype (active_transactions.roots)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", blocks_count, sizeof (decltype (active_transactions.blocks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "election_winner_details", active_transactions.election_winner_details_size (), sizeof (decltype (active_transactions.election_winner_details)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "recently_confirmed", recently_confirmed_count, sizeof (decltype (active_transactions.recently_confirmed)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "recently_cemented", recently_cemented_count, sizeof (decltype (active_transactions.recently_cemented)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "priority_wallet_cementable_frontiers_count", active_transactions.priority_wallet_cementable_frontiers_size (), sizeof (nano::cementable_account) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "priority_cementable_frontiers_count", active_transactions.priority_cementable_frontiers_size (), sizeof (nano::cementable_account) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "inactive_votes_cache_count", active_transactions.inactive_votes_cache_size (), sizeof (nano::gap_information) }));
	return composite;
}
