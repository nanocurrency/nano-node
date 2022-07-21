#include <nano/lib/threading.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/confirmation_height_processor.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/secure/store.hpp>

#include <boost/format.hpp>
#include <boost/variant/get.hpp>

#include <numeric>

using namespace std::chrono;

nano::active_transactions::active_transactions (nano::node & node_a, nano::confirmation_height_processor & confirmation_height_processor_a) :
	scheduler{ node_a.scheduler }, // Move dependencies requiring this circular reference
	confirmation_height_processor{ confirmation_height_processor_a },
	node{ node_a },
	generator{ node_a.config, node_a.ledger, node_a.wallets, node_a.vote_processor, node_a.history, node_a.network, node_a.stats, false },
	final_generator{ node_a.config, node_a.ledger, node_a.wallets, node_a.vote_processor, node_a.history, node_a.network, node_a.stats, true },
	election_time_to_live{ node_a.network_params.network.is_dev_network () ? 0s : 2s },
	thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::request_loop);
		request_loop ();
	})
{
	// Register a callback which will get called after a block is cemented
	confirmation_height_processor.add_cemented_observer ([this] (std::shared_ptr<nano::block> const & callback_block_a) {
		this->block_cemented_callback (callback_block_a);
	});

	// Register a callback which will get called if a block is already cemented
	confirmation_height_processor.add_block_already_cemented_observer ([this] (nano::block_hash const & hash_a) {
		this->block_already_cemented_callback (hash_a);
	});

	nano::unique_lock<nano::mutex> lock (mutex);
	condition.wait (lock, [&started = started] { return started; });
}

nano::active_transactions::~active_transactions ()
{
	stop ();
}

void nano::active_transactions::block_cemented_callback (std::shared_ptr<nano::block> const & block_a)
{
	auto transaction = node.store.tx_begin_read ();

	boost::optional<nano::election_status_type> election_status_type;
	if (!confirmation_height_processor.is_processing_added_block (block_a->hash ()))
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
			nano::account account{};
			nano::uint128_t amount (0);
			bool is_state_send (false);
			bool is_state_epoch (false);
			nano::account pending_account{};
			node.process_confirmed_data (transaction, block_a, block_a->hash (), account, amount, is_state_send, is_state_epoch, pending_account);
			node.observers.blocks.notify (nano::election_status{ block_a, 0, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), 0, 1, 0, nano::election_status_type::inactive_confirmation_height }, {}, account, amount, is_state_send, is_state_epoch);
		}
		else
		{
			auto hash (block_a->hash ());
			nano::unique_lock<nano::mutex> election_winners_lk (election_winner_details_mutex);
			auto existing (election_winner_details.find (hash));
			if (existing != election_winner_details.end ())
			{
				auto election = existing->second;
				election_winner_details.erase (hash);
				election_winners_lk.unlock ();
				if (election->confirmed () && election->winner ()->hash () == hash)
				{
					nano::unique_lock<nano::mutex> election_lk (election->mutex);
					auto status_l = election->status;
					election_lk.unlock ();
					add_recently_cemented (status_l);
					auto destination (block_a->link ().is_zero () ? block_a->destination () : block_a->link ().as_account ());
					node.receive_confirmed (transaction, hash, destination);
					nano::account account{};
					nano::uint128_t amount (0);
					bool is_state_send (false);
					bool is_state_epoch (false);
					nano::account pending_account{};
					node.process_confirmed_data (transaction, block_a, hash, account, amount, is_state_send, is_state_epoch, pending_account);
					election_lk.lock ();
					election->status.type = *election_status_type;
					election->status.confirmation_request_count = election->confirmation_request_count;
					status_l = election->status;
					election_lk.unlock ();
					auto votes (election->votes_with_weight ());
					node.observers.blocks.notify (status_l, votes, account, amount, is_state_send, is_state_epoch);
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

		auto const & account (!block_a->account ().is_zero () ? block_a->account () : block_a->sideband ().account);
		debug_assert (!account.is_zero ());
		if (!node.ledger.cache.final_votes_confirmation_canary.load () && account == node.network_params.ledger.final_votes_canary_account && block_a->sideband ().height >= node.network_params.ledger.final_votes_canary_height)
		{
			node.ledger.cache.final_votes_confirmation_canary.store (true);
		}

		// Next-block activations are done after cementing hardcoded bootstrap count to allow confirming very large chains without interference
		bool const cemented_bootstrap_count_reached{ node.ledger.cache.cemented_count >= node.ledger.bootstrap_weight_max_blocks };

		// Next-block activations are only done for blocks with previously active elections
		bool const was_active{ *election_status_type == nano::election_status_type::active_confirmed_quorum || *election_status_type == nano::election_status_type::active_confirmation_height };

		if (cemented_bootstrap_count_reached && was_active)
		{
			// Start or vote for the next unconfirmed block
			scheduler.activate (account, transaction);

			// Start or vote for the next unconfirmed block in the destination account
			auto const & destination (node.ledger.block_destination (transaction, *block_a));
			if (!destination.is_zero () && destination != account)
			{
				scheduler.activate (destination, transaction);
			}
		}
	}
}

void nano::active_transactions::add_election_winner_details (nano::block_hash const & hash_a, std::shared_ptr<nano::election> const & election_a)
{
	nano::lock_guard<nano::mutex> guard (election_winner_details_mutex);
	election_winner_details.emplace (hash_a, election_a);
}

void nano::active_transactions::remove_election_winner_details (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> guard (election_winner_details_mutex);
	election_winner_details.erase (hash_a);
}

void nano::active_transactions::block_already_cemented_callback (nano::block_hash const & hash_a)
{
	// Depending on timing there is a situation where the election_winner_details is not reset.
	// This can happen when a block wins an election, and the block is confirmed + observer
	// called before the block hash gets added to election_winner_details. If the block is confirmed
	// callbacks have already been done, so we can safely just remove it.
	remove_election_winner_details (hash_a);
}

int64_t nano::active_transactions::vacancy () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto result = static_cast<int64_t> (node.config.active_elections_size) - static_cast<int64_t> (roots.size ());
	return result;
}

void nano::active_transactions::request_confirm (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	std::size_t const this_loop_target_l (roots.size ());
	auto const elections_l{ list_active_impl (this_loop_target_l) };

	lock_a.unlock ();

	nano::confirmation_solicitor solicitor (node.network, node.config);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<std::size_t>::max ()));
	nano::vote_generator_session generator_session (generator);
	nano::vote_generator_session final_generator_session (generator);

	std::size_t unconfirmed_count_l (0);
	nano::timer<std::chrono::milliseconds> elapsed (nano::timer_state::started);

	/*
	 * Loop through active elections in descending order of proof-of-work difficulty, requesting confirmation
	 *
	 * Only up to a certain amount of elections are queued for confirmation request and block rebroadcasting. The remaining elections can still be confirmed if votes arrive
	 * Elections extending the soft config.active_elections_size limit are flushed after a certain time-to-live cutoff
	 * Flushed elections are later re-activated via frontier confirmation
	 */
	for (auto const & election_l : elections_l)
	{
		bool const confirmed_l (election_l->confirmed ());
		unconfirmed_count_l += !confirmed_l;

		if (election_l->transition_time (solicitor))
		{
			// Locks active mutex, cleans up the election and erases it from the main container
			if (!confirmed_l)
			{
				node.stats.inc (nano::stat::type::election, nano::stat::detail::election_drop_expired);
			}
			erase (election_l->qualified_root);
		}
	}

	solicitor.flush ();
	generator_session.flush ();
	final_generator_session.flush ();
	lock_a.lock ();

	if (node.config.logging.timing_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Processed %1% elections (%2% were already confirmed) in %3% %4%") % this_loop_target_l % (this_loop_target_l - unconfirmed_count_l) % elapsed.value ().count () % elapsed.unit ()));
	}
}

void nano::active_transactions::cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election> election)
{
	debug_assert (lock_a.owns_lock ());

	if (!election->confirmed ())
	{
		node.stats.inc (nano::stat::type::election, nano::stat::detail::election_drop_all);
		if (election->behavior == election_behavior::hinted)
		{
			node.stats.inc (nano::stat::type::election, nano::stat::detail::election_hinted_drop);
		}
	}
	else
	{
		node.stats.inc (nano::stat::type::election, nano::stat::detail::election_confirmed_all);
		if (election->behavior == election_behavior::hinted)
		{
			node.stats.inc (nano::stat::type::election, nano::stat::detail::election_hinted_confirmed);
		}
	}

	if (election->behavior == election_behavior::hinted)
	{
		--active_hinted_elections_count;
	}

	auto blocks_l = election->blocks ();
	for (auto const & [hash, block] : blocks_l)
	{
		auto erased (blocks.erase (hash));
		(void)erased;
		debug_assert (erased == 1);
		erase_inactive_votes_cache (hash);
	}
	roots.get<tag_root> ().erase (roots.get<tag_root> ().find (election->qualified_root));

	lock_a.unlock ();
	vacancy_update ();
	for (auto const & [hash, block] : blocks_l)
	{
		// Notify observers about dropped elections & blocks lost confirmed elections
		if (!election->confirmed () || hash != election->winner ()->hash ())
		{
			node.observers.active_stopped.notify (hash);
		}

		if (!election->confirmed ())
		{
			// Clear from publish filter
			node.network.publish_filter.clear (block);
		}
	}

	node.stats.inc (nano::stat::type::election, election->confirmed () ? nano::stat::detail::election_confirmed : nano::stat::detail::election_not_confirmed);
	if (node.config.logging.election_result_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Election erased for root %1%, confirmed: %2$b") % election->qualified_root.to_string () % election->confirmed ()));
	}
}

std::vector<std::shared_ptr<nano::election>> nano::active_transactions::list_active (std::size_t max_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	return list_active_impl (max_a);
}

std::vector<std::shared_ptr<nano::election>> nano::active_transactions::list_active_impl (std::size_t max_a) const
{
	std::vector<std::shared_ptr<nano::election>> result_l;
	result_l.reserve (std::min (max_a, roots.size ()));
	{
		auto & sorted_roots_l (roots.get<tag_random_access> ());
		std::size_t count_l{ 0 };
		for (auto i = sorted_roots_l.begin (), n = sorted_roots_l.end (); i != n && count_l < max_a; ++i, ++count_l)
		{
			result_l.push_back (i->election);
		}
	}
	return result_l;
}

void nano::active_transactions::request_loop ()
{
	nano::unique_lock<nano::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();

	// The wallets and active_transactions objects are mutually dependent, so we need a fully
	// constructed node before proceeding.
	this->node.node_initialized_latch.wait ();

	lock.lock ();

	while (!stopped && !node.flags.disable_request_loop)
	{
		// If many votes are queued, ensure at least the currently active ones finish processing
		lock.unlock ();
		condition.notify_all ();
		if (node.vote_processor.half_full ())
		{
			node.vote_processor.flush ();
		}
		lock.lock ();

		auto const stamp_l = std::chrono::steady_clock::now ();

		request_confirm (lock);
		debug_assert (lock.owns_lock ());

		if (!stopped)
		{
			auto const min_sleep_l = std::chrono::milliseconds (node.network_params.network.request_interval_ms / 2);
			auto const wakeup_l = std::max (stamp_l + std::chrono::milliseconds (node.network_params.network.request_interval_ms), std::chrono::steady_clock::now () + min_sleep_l);
			condition.wait_until (lock, wakeup_l, [&wakeup_l, &stopped = stopped] { return stopped || std::chrono::steady_clock::now () >= wakeup_l; });
		}
	}
}

void nano::active_transactions::stop ()
{
	nano::unique_lock<nano::mutex> lock (mutex);
	if (!started)
	{
		condition.wait (lock, [&started = started] { return started; });
	}
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	generator.stop ();
	final_generator.stop ();
	lock.lock ();
	roots.clear ();
}

nano::election_insertion_result nano::active_transactions::insert_impl (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::block> const & block_a, nano::election_behavior election_behavior_a, std::function<void (std::shared_ptr<nano::block> const &)> const & confirmation_action_a)
{
	debug_assert (lock_a.owns_lock ());
	debug_assert (block_a->has_sideband ());
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
				result.election = nano::make_shared<nano::election> (
				node, block_a, confirmation_action_a, [&node = node] (auto const & rep_a) {
					// Representative is defined as online if replying to live votes or rep_crawler queries
					node.online_reps.observe (rep_a);
				},
				election_behavior_a);
				roots.get<tag_root> ().emplace (nano::active_transactions::conflict_info{ root, result.election });
				blocks.emplace (hash, result.election);
				// Increase hinted election counter while still holding lock
				if (election_behavior_a == election_behavior::hinted)
				{
					active_hinted_elections_count++;
				}
				auto const cache = find_inactive_votes_cache_impl (hash);
				lock_a.unlock ();
				cache.fill (result.election);
				node.observers.active_started.notify (hash);
				node.stats.inc (nano::stat::type::election, nano::stat::detail::election_start);
				vacancy_update ();
			}
		}
		else
		{
			result.election = existing->election;
		}

		if (lock_a.owns_lock ())
		{
			lock_a.unlock ();
		}

		// Votes are generated for inserted or ongoing elections
		if (result.election)
		{
			result.election->generate_votes ();
		}
	}
	return result;
}

nano::election_insertion_result nano::active_transactions::insert_hinted (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::block> const & block_a)
{
	debug_assert (lock_a.owns_lock ());

	const std::size_t limit = node.config.active_elections_hinted_limit_percentage * node.config.active_elections_size / 100;
	if (active_hinted_elections_count >= limit)
	{
		// Reached maximum number of hinted elections, drop new ones
		node.stats.inc (nano::stat::type::election, nano::stat::detail::election_hinted_overflow);
		return {};
	}

	auto result = insert_impl (lock_a, block_a, nano::election_behavior::hinted);
	if (result.inserted)
	{
		node.stats.inc (nano::stat::type::election, nano::stat::detail::election_hinted_started);
	}
	return result;
}

// Validate a vote and apply it to the current election if one exists
nano::vote_code nano::active_transactions::vote (std::shared_ptr<nano::vote> const & vote_a)
{
	nano::vote_code result{ nano::vote_code::indeterminate };
	// If all hashes were recently confirmed then it is a replay
	unsigned recently_confirmed_counter (0);
	std::vector<std::pair<std::shared_ptr<nano::election>, nano::block_hash>> process;
	{
		nano::unique_lock<nano::mutex> lock (mutex);
		for (auto const & hash : vote_a->hashes)
		{
			auto & recently_confirmed_by_hash (recently_confirmed.get<tag_hash> ());
			auto existing (blocks.find (hash));
			if (existing != blocks.end ())
			{
				process.emplace_back (existing->second, hash);
			}
			else if (recently_confirmed_by_hash.count (hash) == 0)
			{
				add_inactive_votes_cache (lock, hash, vote_a->account, vote_a->timestamp ());
			}
			else
			{
				++recently_confirmed_counter;
			}
		}
	}

	if (!process.empty ())
	{
		bool replay (false);
		bool processed (false);
		for (auto const & [election, block_hash] : process)
		{
			auto const result_l = election->vote (vote_a->account, vote_a->timestamp (), block_hash);
			processed = processed || result_l.processed;
			replay = replay || result_l.replay;
		}

		// Republish vote if it is new and the node does not host a principal representative (or close to)
		if (processed)
		{
			auto const reps (node.wallets.reps ());
			if (!reps.have_half_rep () && !reps.exists (vote_a->account))
			{
				node.network.flood_vote (vote_a, 0.5f);
			}
		}
		result = replay ? nano::vote_code::replay : nano::vote_code::vote;
	}
	else if (recently_confirmed_counter == vote_a->hashes.size ())
	{
		result = nano::vote_code::replay;
	}
	return result;
}

bool nano::active_transactions::active (nano::qualified_root const & root_a)
{
	nano::lock_guard<nano::mutex> lock (mutex);
	return roots.get<tag_root> ().find (root_a) != roots.get<tag_root> ().end ();
}

bool nano::active_transactions::active (nano::block const & block_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	return roots.get<tag_root> ().find (block_a.qualified_root ()) != roots.get<tag_root> ().end () && blocks.find (block_a.hash ()) != blocks.end ();
}

std::shared_ptr<nano::election> nano::active_transactions::election (nano::qualified_root const & root_a) const
{
	std::shared_ptr<nano::election> result;
	nano::lock_guard<nano::mutex> lock (mutex);
	auto existing = roots.get<tag_root> ().find (root_a);
	if (existing != roots.get<tag_root> ().end ())
	{
		result = existing->election;
	}
	return result;
}

std::shared_ptr<nano::block> nano::active_transactions::winner (nano::block_hash const & hash_a) const
{
	std::shared_ptr<nano::block> result;
	nano::unique_lock<nano::mutex> lock (mutex);
	auto existing = blocks.find (hash_a);
	if (existing != blocks.end ())
	{
		auto election = existing->second;
		lock.unlock ();
		result = election->winner ();
	}
	return result;
}

std::deque<nano::election_status> nano::active_transactions::list_recently_cemented ()
{
	nano::lock_guard<nano::mutex> lock (mutex);
	return recently_cemented;
}

void nano::active_transactions::add_recently_cemented (nano::election_status const & status_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	recently_cemented.push_back (status_a);
	if (recently_cemented.size () > node.config.confirmation_history_size)
	{
		recently_cemented.pop_front ();
	}
}

void nano::active_transactions::add_recently_confirmed (nano::qualified_root const & root_a, nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	recently_confirmed.get<tag_sequence> ().emplace_back (root_a, hash_a);
	if (recently_confirmed.size () > recently_confirmed_size)
	{
		recently_confirmed.get<tag_sequence> ().pop_front ();
	}
}

void nano::active_transactions::erase_recently_confirmed (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	recently_confirmed.get<tag_hash> ().erase (hash_a);
}

void nano::active_transactions::erase (nano::block const & block_a)
{
	erase (block_a.qualified_root ());
}

void nano::active_transactions::erase (nano::qualified_root const & root_a)
{
	nano::unique_lock<nano::mutex> lock (mutex);
	auto root_it (roots.get<tag_root> ().find (root_a));
	if (root_it != roots.get<tag_root> ().end ())
	{
		cleanup_election (lock, root_it->election);
	}
}

void nano::active_transactions::erase_hash (nano::block_hash const & hash_a)
{
	nano::unique_lock<nano::mutex> lock (mutex);
	[[maybe_unused]] auto erased (blocks.erase (hash_a));
	debug_assert (erased == 1);
}

void nano::active_transactions::erase_oldest ()
{
	nano::unique_lock<nano::mutex> lock (mutex);
	if (!roots.empty ())
	{
		node.stats.inc (nano::stat::type::election, nano::stat::detail::election_drop_overflow);
		auto item = roots.get<tag_random_access> ().front ();
		cleanup_election (lock, item.election);
	}
}

bool nano::active_transactions::empty ()
{
	nano::lock_guard<nano::mutex> lock (mutex);
	return roots.empty ();
}

std::size_t nano::active_transactions::size ()
{
	nano::lock_guard<nano::mutex> lock (mutex);
	return roots.size ();
}

bool nano::active_transactions::publish (std::shared_ptr<nano::block> const & block_a)
{
	nano::unique_lock<nano::mutex> lock (mutex);
	auto existing (roots.get<tag_root> ().find (block_a->qualified_root ()));
	auto result (true);
	if (existing != roots.get<tag_root> ().end ())
	{
		auto election (existing->election);
		lock.unlock ();
		result = election->publish (block_a);
		if (!result)
		{
			lock.lock ();
			blocks.emplace (block_a->hash (), election);
			auto const cache = find_inactive_votes_cache_impl (block_a->hash ());
			lock.unlock ();
			cache.fill (election);
			node.stats.inc (nano::stat::type::election, nano::stat::detail::election_block_conflict);
		}
	}
	return result;
}

// Returns the type of election status requiring callbacks calling later
boost::optional<nano::election_status_type> nano::active_transactions::confirm_block (nano::transaction const & transaction_a, std::shared_ptr<nano::block> const & block_a)
{
	auto hash (block_a->hash ());
	nano::unique_lock<nano::mutex> lock (mutex);
	auto existing (blocks.find (hash));
	boost::optional<nano::election_status_type> status_type;
	if (existing != blocks.end ())
	{
		lock.unlock ();
		nano::unique_lock<nano::mutex> election_lock (existing->second->mutex);
		if (existing->second->status.winner && existing->second->status.winner->hash () == hash)
		{
			if (!existing->second->confirmed ())
			{
				existing->second->confirm_once (election_lock, nano::election_status_type::active_confirmation_height);
				status_type = nano::election_status_type::active_confirmation_height;
			}
			else
			{
#ifndef NDEBUG
				nano::unique_lock<nano::mutex> election_winners_lk (election_winner_details_mutex);
				debug_assert (election_winner_details.find (hash) != election_winner_details.cend ());
#endif
				status_type = nano::election_status_type::active_confirmed_quorum;
			}
		}
		else
		{
			status_type = boost::optional<nano::election_status_type>{};
		}
	}
	else
	{
		status_type = nano::election_status_type::inactive_confirmation_height;
	}

	return status_type;
}

std::size_t nano::active_transactions::inactive_votes_cache_size ()
{
	nano::lock_guard<nano::mutex> guard (mutex);
	return inactive_votes_cache.size ();
}

void nano::active_transactions::add_inactive_votes_cache (nano::unique_lock<nano::mutex> & lock_a, nano::block_hash const & hash_a, nano::account const & representative_a, uint64_t const timestamp_a)
{
	if (node.flags.inactive_votes_cache_size == 0)
	{
		return;
	}

	// Check principal representative status
	if (node.ledger.weight (representative_a) > node.minimum_principal_weight ())
	{
		/** It is important that the new vote is added to the cache before calling inactive_votes_bootstrap_check
		 * This guarantees consistency when a vote is received while also receiving the corresponding block
		 */
		auto & inactive_by_hash (inactive_votes_cache.get<tag_hash> ());
		auto existing (inactive_by_hash.find (hash_a));
		if (existing != inactive_by_hash.end ())
		{
			if (existing->needs_eval ())
			{
				auto is_new (false);
				inactive_by_hash.modify (existing, [representative_a, timestamp_a, &is_new] (nano::inactive_cache_information & info) {
					auto it = std::find_if (info.voters.begin (), info.voters.end (), [&representative_a] (auto const & item_a) { return item_a.first == representative_a; });
					is_new = (it == info.voters.end ());
					if (is_new)
					{
						info.arrival = std::chrono::steady_clock::now ();
						info.voters.emplace_back (representative_a, timestamp_a);
					}
				});

				if (is_new)
				{
					auto const old_status = existing->status;
					auto const status = inactive_votes_bootstrap_check (lock_a, existing->voters, hash_a, existing->status);
					if (status != old_status)
					{
						// The lock has since been released
						existing = inactive_by_hash.find (hash_a);
						if (existing != inactive_by_hash.end ())
						{
							inactive_by_hash.modify (existing, [status] (nano::inactive_cache_information & info) {
								info.status = status;
							});
						}
					}
				}
			}
		}
		else
		{
			auto & inactive_by_arrival (inactive_votes_cache.get<tag_arrival> ());
			nano::inactive_cache_status default_status{};
			inactive_by_arrival.emplace (nano::inactive_cache_information{ std::chrono::steady_clock::now (), hash_a, representative_a, timestamp_a, default_status });
			auto const status (inactive_votes_bootstrap_check (lock_a, representative_a, hash_a, default_status));
			if (status != default_status)
			{
				// The lock has since been released
				existing = inactive_by_hash.find (hash_a);
				if (existing != inactive_by_hash.end ())
				{
					inactive_by_hash.modify (existing, [status] (nano::inactive_cache_information & info) {
						info.status = status;
					});
				}
			}
			if (inactive_votes_cache.size () > node.flags.inactive_votes_cache_size)
			{
				inactive_by_arrival.erase (inactive_by_arrival.begin ());
			}
		}
	}
}

void nano::active_transactions::trigger_inactive_votes_cache_election (std::shared_ptr<nano::block> const & block_a)
{
	nano::unique_lock<nano::mutex> lock (mutex);
	auto const status = find_inactive_votes_cache_impl (block_a->hash ()).status;
	if (status.election_started)
	{
		insert_hinted (lock, block_a);
	}
}

nano::inactive_cache_information nano::active_transactions::find_inactive_votes_cache (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	return find_inactive_votes_cache_impl (hash_a);
}

nano::inactive_cache_information nano::active_transactions::find_inactive_votes_cache_impl (nano::block_hash const & hash_a)
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

nano::inactive_cache_status nano::active_transactions::inactive_votes_bootstrap_check (nano::unique_lock<nano::mutex> & lock_a, nano::account const & voter_a, nano::block_hash const & hash_a, nano::inactive_cache_status const & previously_a)
{
	debug_assert (lock_a.owns_lock ());
	lock_a.unlock ();
	return inactive_votes_bootstrap_check_impl (lock_a, node.ledger.weight (voter_a), 1, hash_a, previously_a);
}

nano::inactive_cache_status nano::active_transactions::inactive_votes_bootstrap_check (nano::unique_lock<nano::mutex> & lock_a, std::vector<std::pair<nano::account, uint64_t>> const & voters_a, nano::block_hash const & hash_a, nano::inactive_cache_status const & previously_a)
{
	/** Perform checks on accumulated tally from inactive votes
	 * These votes are generally either for unconfirmed blocks or old confirmed blocks
	 * That check is made after hitting a tally threshold, and always as late and as few times as possible
	 */
	debug_assert (lock_a.owns_lock ());
	lock_a.unlock ();

	nano::uint128_t tally;
	for (auto const & [voter, timestamp] : voters_a)
	{
		tally += node.ledger.weight (voter);
	}

	return inactive_votes_bootstrap_check_impl (lock_a, tally, voters_a.size (), hash_a, previously_a);
}

nano::inactive_cache_status nano::active_transactions::inactive_votes_bootstrap_check_impl (nano::unique_lock<nano::mutex> & lock_a, nano::uint128_t const & tally_a, std::size_t voters_size_a, nano::block_hash const & hash_a, nano::inactive_cache_status const & previously_a)
{
	debug_assert (!lock_a.owns_lock ());
	nano::inactive_cache_status status (previously_a);
	const unsigned election_start_voters_min = node.network_params.network.is_dev_network () ? 2 : node.network_params.network.is_beta_network () ? 5
																																				  : 15;
	status.tally = tally_a;
	if (!previously_a.confirmed && tally_a >= node.online_reps.delta ())
	{
		status.bootstrap_started = true;
		status.confirmed = true;
	}
	else if (!previously_a.bootstrap_started && !node.flags.disable_legacy_bootstrap && node.flags.disable_lazy_bootstrap && tally_a > node.gap_cache.bootstrap_threshold ())
	{
		status.bootstrap_started = true;
	}
	if (!previously_a.election_started && voters_size_a >= election_start_voters_min && tally_a >= (node.online_reps.trended () / 100) * node.config.election_hint_weight_percent)
	{
		status.election_started = true;
	}

	if ((status.election_started && !previously_a.election_started) || (status.bootstrap_started && !previously_a.bootstrap_started))
	{
		auto transaction (node.store.tx_begin_read ());
		auto block = node.store.block.get (transaction, hash_a);
		if (block && status.election_started && !previously_a.election_started && !node.block_confirmed_or_being_confirmed (transaction, hash_a))
		{
			lock_a.lock ();
			auto result = insert_hinted (lock_a, block);
			if (!result.inserted && result.election == nullptr)
			{
				status.election_started = false;
			}
		}
		else if (!block && status.bootstrap_started && !previously_a.bootstrap_started && (!node.ledger.pruning || !node.store.pruned.exists (transaction, hash_a)))
		{
			node.gap_cache.bootstrap_start (hash_a);
		}
	}

	if (!lock_a.owns_lock ())
	{
		lock_a.lock ();
	}

	return status;
}

bool nano::purge_singleton_inactive_votes_cache_pool_memory ()
{
	return boost::singleton_pool<boost::fast_pool_allocator_tag, sizeof (nano::active_transactions::ordered_cache::node_type)>::purge_memory ();
}

std::size_t nano::active_transactions::election_winner_details_size ()
{
	nano::lock_guard<nano::mutex> guard (election_winner_details_mutex);
	return election_winner_details.size ();
}

nano::cementable_account::cementable_account (nano::account const & account_a, std::size_t blocks_uncemented_a) :
	account (account_a), blocks_uncemented (blocks_uncemented_a)
{
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (active_transactions & active_transactions, std::string const & name)
{
	std::size_t roots_count;
	std::size_t blocks_count;
	std::size_t recently_confirmed_count;
	std::size_t recently_cemented_count;

	{
		nano::lock_guard<nano::mutex> guard (active_transactions.mutex);
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
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "inactive_votes_cache", active_transactions.inactive_votes_cache_size (), sizeof (nano::gap_information) }));
	composite->add_component (collect_container_info (active_transactions.generator, "generator"));
	return composite;
}
