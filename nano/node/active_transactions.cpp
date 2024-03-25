#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/component.hpp>

#include <boost/format.hpp>

using namespace std::chrono;

nano::active_transactions::active_transactions (nano::node & node_a, nano::confirming_set & confirming_set, nano::block_processor & block_processor_a) :
	node{ node_a },
	confirming_set{ confirming_set },
	block_processor{ block_processor_a },
	recently_confirmed{ 65536 },
	recently_cemented{ node.config.confirmation_history_size },
	election_time_to_live{ node_a.network_params.network.is_dev_network () ? 0s : 2s }
{
	count_by_behavior.fill (0); // Zero initialize array

	// Register a callback which will get called after a block is cemented
	confirming_set.cemented_observers.add ([this] (std::shared_ptr<nano::block> const & callback_block_a) {
		this->block_cemented_callback (callback_block_a);
	});

	// Register a callback which will get called if a block is already cemented
	confirming_set.block_already_cemented_observers.add ([this] (nano::block_hash const & hash_a) {
		this->block_already_cemented_callback (hash_a);
	});

	// Notify elections about alternative (forked) blocks
	block_processor.block_processed.add ([this] (auto const & result, auto const & context) {
		switch (result)
		{
			case nano::block_status::fork:
				publish (context.block);
				break;
			default:
				break;
		}
	});
}

nano::active_transactions::~active_transactions ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::active_transactions::start ()
{
	if (node.flags.disable_request_loop)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::request_loop);
		request_loop ();
	});
}

void nano::active_transactions::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
	clear ();
}

void nano::active_transactions::block_cemented_callback (std::shared_ptr<nano::block> const & block)
{
	debug_assert (node.block_confirmed (block->hash ()));
	if (auto election_l = election (block->qualified_root ()))
	{
		election_l->try_confirm (block->hash ());
	}
	auto election = remove_election_winner_details (block->hash ());
	nano::election_status status;
	std::vector<nano::vote_with_weight_info> votes;
	status.winner = block;
	if (election)
	{
		status = election->get_status ();
		votes = election->votes_with_weight ();
	}
	if (confirming_set.exists (block->hash ()))
	{
		status.type = nano::election_status_type::active_confirmed_quorum;
	}
	else if (election)
	{
		status.type = nano::election_status_type::active_confirmation_height;
	}
	else
	{
		status.type = nano::election_status_type::inactive_confirmation_height;
	}
	recently_cemented.put (status);
	auto transaction = node.store.tx_begin_read ();
	notify_observers (transaction, status, votes);
	bool cemented_bootstrap_count_reached = node.ledger.cache.cemented_count >= node.ledger.bootstrap_weight_max_blocks;
	bool was_active = status.type == nano::election_status_type::active_confirmed_quorum || status.type == nano::election_status_type::active_confirmation_height;

	// Next-block activations are only done for blocks with previously active elections
	if (cemented_bootstrap_count_reached && was_active)
	{
		activate_successors (transaction, block);
	}
}

void nano::active_transactions::notify_observers (nano::store::read_transaction const & transaction, nano::election_status const & status, std::vector<nano::vote_with_weight_info> const & votes)
{
	auto block = status.winner;
	auto account = block->account ();
	auto amount = node.ledger.amount (transaction, block->hash ()).value_or (0);
	auto is_state_send = block->type () == block_type::state && block->is_send ();
	auto is_state_epoch = block->type () == block_type::state && block->is_epoch ();
	node.observers.blocks.notify (status, votes, account, amount, is_state_send, is_state_epoch);

	if (amount > 0)
	{
		node.observers.account_balance.notify (account, false);
		if (block->is_send ())
		{
			node.observers.account_balance.notify (block->destination (), true);
		}
	}
}

void nano::active_transactions::activate_successors (nano::store::read_transaction const & transaction, std::shared_ptr<nano::block> const & block)
{
	node.scheduler.priority.activate (block->account (), transaction);

	// Start or vote for the next unconfirmed block in the destination account
	if (block->is_send () && !block->destination ().is_zero () && block->destination () != block->account ())
	{
		node.scheduler.priority.activate (block->destination (), transaction);
	}
}

void nano::active_transactions::add_election_winner_details (nano::block_hash const & hash_a, std::shared_ptr<nano::election> const & election_a)
{
	nano::lock_guard<nano::mutex> guard{ election_winner_details_mutex };
	election_winner_details.emplace (hash_a, election_a);
}

std::shared_ptr<nano::election> nano::active_transactions::remove_election_winner_details (nano::block_hash const & hash_a)
{
	nano::lock_guard<nano::mutex> guard{ election_winner_details_mutex };
	std::shared_ptr<nano::election> result;
	auto existing = election_winner_details.find (hash_a);
	if (existing != election_winner_details.end ())
	{
		result = existing->second;
		election_winner_details.erase (existing);
	}
	return result;
}

void nano::active_transactions::block_already_cemented_callback (nano::block_hash const & hash_a)
{
	// Depending on timing there is a situation where the election_winner_details is not reset.
	// This can happen when a block wins an election, and the block is confirmed + observer
	// called before the block hash gets added to election_winner_details. If the block is confirmed
	// callbacks have already been done, so we can safely just remove it.
	remove_election_winner_details (hash_a);
}

int64_t nano::active_transactions::limit (nano::election_behavior behavior) const
{
	switch (behavior)
	{
		case nano::election_behavior::normal:
		{
			return static_cast<int64_t> (node.config.active_elections_size);
		}
		case nano::election_behavior::hinted:
		{
			const uint64_t limit = node.config.active_elections_hinted_limit_percentage * node.config.active_elections_size / 100;
			return static_cast<int64_t> (limit);
		}
		case nano::election_behavior::optimistic:
		{
			const uint64_t limit = node.config.active_elections_optimistic_limit_percentage * node.config.active_elections_size / 100;
			return static_cast<int64_t> (limit);
		}
	}

	debug_assert (false, "unknown election behavior");
	return 0;
}

int64_t nano::active_transactions::vacancy (nano::election_behavior behavior) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	switch (behavior)
	{
		case nano::election_behavior::normal:
			return limit () - static_cast<int64_t> (roots.size ());
		case nano::election_behavior::hinted:
		case nano::election_behavior::optimistic:
			return limit (behavior) - count_by_behavior[behavior];
	}
	debug_assert (false); // Unknown enum
	return 0;
}

void nano::active_transactions::request_confirm (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	std::size_t const this_loop_target_l (roots.size ());
	auto const elections_l{ list_active_impl (this_loop_target_l) };

	lock_a.unlock ();

	nano::confirmation_solicitor solicitor (node.network, node.config);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<std::size_t>::max ()));

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
			erase (election_l->qualified_root);
		}
	}

	solicitor.flush ();
	lock_a.lock ();
}

void nano::active_transactions::cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election> election)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock_a.owns_lock ());
	debug_assert (!election->confirmed () || recently_confirmed.exists (election->qualified_root));

	// Keep track of election count by election type
	debug_assert (count_by_behavior[election->behavior ()] > 0);
	count_by_behavior[election->behavior ()]--;

	auto blocks_l = election->blocks ();
	for (auto const & [hash, block] : blocks_l)
	{
		auto erased (blocks.erase (hash));
		(void)erased;
		debug_assert (erased == 1);
	}

	roots.get<tag_root> ().erase (roots.get<tag_root> ().find (election->qualified_root));

	node.stats.inc (completion_type (*election), to_stat_detail (election->behavior ()));
	node.logger.trace (nano::log::type::active_transactions, nano::log::detail::active_stopped, nano::log::arg{ "election", election });

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
}

nano::stat::type nano::active_transactions::completion_type (nano::election const & election) const
{
	if (election.confirmed ())
	{
		return nano::stat::type::active_confirmed;
	}
	if (election.failed ())
	{
		return nano::stat::type::active_timeout;
	}
	return nano::stat::type::active_dropped;
}

std::vector<std::shared_ptr<nano::election>> nano::active_transactions::list_active (std::size_t max_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return list_active_impl (max_a);
}

std::vector<std::shared_ptr<nano::election>> nano::active_transactions::list_active_impl (std::size_t max_a) const
{
	std::vector<std::shared_ptr<nano::election>> result_l;
	result_l.reserve (std::min (max_a, roots.size ()));
	{
		auto & sorted_roots_l (roots.get<tag_sequenced> ());
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
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto const stamp_l = std::chrono::steady_clock::now ();

		node.stats.inc (nano::stat::type::active, nano::stat::detail::loop);

		request_confirm (lock);
		debug_assert (lock.owns_lock ());

		if (!stopped)
		{
			auto const min_sleep_l = std::chrono::milliseconds (node.network_params.network.aec_loop_interval_ms / 2);
			auto const wakeup_l = std::max (stamp_l + std::chrono::milliseconds (node.network_params.network.aec_loop_interval_ms), std::chrono::steady_clock::now () + min_sleep_l);
			condition.wait_until (lock, wakeup_l, [&wakeup_l, &stopped = stopped] { return stopped || std::chrono::steady_clock::now () >= wakeup_l; });
		}
	}
}

void nano::active_transactions::trim ()
{
	/*
	 * Both normal and hinted election schedulers are well-behaved, meaning they first check for AEC vacancy before inserting new elections.
	 * However, it is possible that AEC will be temporarily overfilled in case it's running at full capacity and election hinting or manual queue kicks in.
	 * That case will lead to unwanted churning of elections, so this allows for AEC to be overfilled to 125% until erasing of elections happens.
	 */
	while (vacancy () < -(limit () / 4))
	{
		node.stats.inc (nano::stat::type::active, nano::stat::detail::erase_oldest);
		erase_oldest ();
	}
}

nano::election_insertion_result nano::active_transactions::insert (std::shared_ptr<nano::block> const & block_a, nano::election_behavior election_behavior_a)
{
	debug_assert (block_a);
	debug_assert (block_a->has_sideband ());

	nano::unique_lock<nano::mutex> lock{ mutex };

	nano::election_insertion_result result;

	if (stopped)
	{
		return result;
	}

	auto const root = block_a->qualified_root ();
	auto const hash = block_a->hash ();
	auto const existing = roots.get<tag_root> ().find (root);
	if (existing == roots.get<tag_root> ().end ())
	{
		if (!recently_confirmed.exists (root))
		{
			result.inserted = true;
			auto observe_rep_cb = [&node = node] (auto const & rep_a) {
				// Representative is defined as online if replying to live votes or rep_crawler queries
				node.online_reps.observe (rep_a);
			};
			result.election = nano::make_shared<nano::election> (node, block_a, nullptr, observe_rep_cb, election_behavior_a);
			roots.get<tag_root> ().emplace (nano::active_transactions::conflict_info{ root, result.election });
			blocks.emplace (hash, result.election);

			// Keep track of election count by election type
			debug_assert (count_by_behavior[result.election->behavior ()] >= 0);
			count_by_behavior[result.election->behavior ()]++;

			node.stats.inc (nano::stat::type::active_started, to_stat_detail (election_behavior_a));
			node.logger.trace (nano::log::type::active_transactions, nano::log::detail::active_started,
			nano::log::arg{ "behavior", election_behavior_a },
			nano::log::arg{ "election", result.election });
		}
		else
		{
			// result is not set
		}
	}
	else
	{
		result.election = existing->election;
	}

	lock.unlock ();

	if (result.inserted)
	{
		debug_assert (result.election);

		trigger_vote_cache (hash);

		node.observers.active_started.notify (hash);
		vacancy_update ();
	}

	// Votes are generated for inserted or ongoing elections
	if (result.election)
	{
		result.election->broadcast_vote ();
	}

	trim ();

	return result;
}

bool nano::active_transactions::trigger_vote_cache (nano::block_hash hash)
{
	auto cached = node.vote_cache.find (hash);
	for (auto const & cached_vote : cached)
	{
		vote (cached_vote, nano::vote_source::cache);
	}
	return !cached.empty ();
}

// Validate a vote and apply it to the current election if one exists
std::unordered_map<nano::block_hash, nano::vote_code> nano::active_transactions::vote (std::shared_ptr<nano::vote> const & vote, nano::vote_source source)
{
	std::unordered_map<nano::block_hash, nano::vote_code> results;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::election>> process;
	std::vector<nano::block_hash> inactive; // Hashes that should be added to inactive vote cache
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		for (auto const & hash : vote->hashes)
		{
			// Ignore duplicate hashes (should not happen with a well-behaved voting node)
			if (results.find (hash) != results.end ())
			{
				continue;
			}

			if (auto existing = blocks.find (hash); existing != blocks.end ())
			{
				process[hash] = existing->second;
			}
			else if (!recently_confirmed.exists (hash))
			{
				inactive.emplace_back (hash);
				results[hash] = nano::vote_code::indeterminate;
			}
			else
			{
				results[hash] = nano::vote_code::replay;
			}
		}
	}

	for (auto const & [block_hash, election] : process)
	{
		auto const vote_result = election->vote (vote->account, vote->timestamp (), block_hash, source);
		results[block_hash] = vote_result;
	}

	// All hashes should have their result set
	debug_assert (std::all_of (vote->hashes.begin (), vote->hashes.end (), [&results] (auto const & hash) {
		return results.find (hash) != results.end ();
	}));

	vote_processed.notify (vote, source, results);

	return results;
}

bool nano::active_transactions::active (nano::qualified_root const & root_a) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return roots.get<tag_root> ().find (root_a) != roots.get<tag_root> ().end ();
}

bool nano::active_transactions::active (nano::block const & block_a) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return roots.get<tag_root> ().find (block_a.qualified_root ()) != roots.get<tag_root> ().end () && blocks.find (block_a.hash ()) != blocks.end ();
}

bool nano::active_transactions::active (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return blocks.find (hash) != blocks.end ();
}

std::shared_ptr<nano::election> nano::active_transactions::election (nano::qualified_root const & root_a) const
{
	std::shared_ptr<nano::election> result;
	nano::lock_guard<nano::mutex> lock{ mutex };
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
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto existing = blocks.find (hash_a);
	if (existing != blocks.end ())
	{
		auto election = existing->second;
		lock.unlock ();
		result = election->winner ();
	}
	return result;
}

bool nano::active_transactions::erase (nano::block const & block_a)
{
	return erase (block_a.qualified_root ());
}

bool nano::active_transactions::erase (nano::qualified_root const & root_a)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto root_it (roots.get<tag_root> ().find (root_a));
	if (root_it != roots.get<tag_root> ().end ())
	{
		cleanup_election (lock, root_it->election);
		return true;
	}
	return false;
}

bool nano::active_transactions::erase_hash (nano::block_hash const & hash_a)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	[[maybe_unused]] auto erased (blocks.erase (hash_a));
	debug_assert (erased == 1);
	return erased == 1;
}

void nano::active_transactions::erase_oldest ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!roots.empty ())
	{
		auto item = roots.get<tag_sequenced> ().front ();
		cleanup_election (lock, item.election);
	}
}

bool nano::active_transactions::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return roots.empty ();
}

std::size_t nano::active_transactions::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return roots.size ();
}

bool nano::active_transactions::publish (std::shared_ptr<nano::block> const & block_a)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
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
			lock.unlock ();

			trigger_vote_cache (block_a->hash ());

			node.stats.inc (nano::stat::type::active, nano::stat::detail::election_block_conflict);
		}
	}
	return result;
}

std::size_t nano::active_transactions::election_winner_details_size ()
{
	nano::lock_guard<nano::mutex> guard{ election_winner_details_mutex };
	return election_winner_details.size ();
}

void nano::active_transactions::clear ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		blocks.clear ();
		roots.clear ();
	}
	vacancy_update ();
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (active_transactions & active_transactions, std::string const & name)
{
	nano::lock_guard<nano::mutex> guard{ active_transactions.mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "roots", active_transactions.roots.size (), sizeof (decltype (active_transactions.roots)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", active_transactions.blocks.size (), sizeof (decltype (active_transactions.blocks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "election_winner_details", active_transactions.election_winner_details_size (), sizeof (decltype (active_transactions.election_winner_details)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "normal", static_cast<std::size_t> (active_transactions.count_by_behavior[nano::election_behavior::normal]), 0 }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "hinted", static_cast<std::size_t> (active_transactions.count_by_behavior[nano::election_behavior::hinted]), 0 }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "optimistic", static_cast<std::size_t> (active_transactions.count_by_behavior[nano::election_behavior::optimistic]), 0 }));

	composite->add_component (active_transactions.recently_confirmed.collect_container_info ("recently_confirmed"));
	composite->add_component (active_transactions.recently_cemented.collect_container_info ("recently_cemented"));

	return composite;
}

/*
 * class recently_confirmed
 */

nano::recently_confirmed_cache::recently_confirmed_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void nano::recently_confirmed_cache::put (const nano::qualified_root & root, const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	confirmed.get<tag_sequence> ().emplace_back (root, hash);
	if (confirmed.size () > max_size)
	{
		confirmed.get<tag_sequence> ().pop_front ();
	}
}

void nano::recently_confirmed_cache::erase (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	confirmed.get<tag_hash> ().erase (hash);
}

void nano::recently_confirmed_cache::clear ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	confirmed.clear ();
}

bool nano::recently_confirmed_cache::exists (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.get<tag_hash> ().find (hash) != confirmed.get<tag_hash> ().end ();
}

bool nano::recently_confirmed_cache::exists (const nano::qualified_root & root) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.get<tag_root> ().find (root) != confirmed.get<tag_root> ().end ();
}

std::size_t nano::recently_confirmed_cache::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.size ();
}

nano::recently_confirmed_cache::entry_t nano::recently_confirmed_cache::back () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.back ();
}

std::unique_ptr<nano::container_info_component> nano::recently_confirmed_cache::collect_container_info (const std::string & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "confirmed", confirmed.size (), sizeof (decltype (confirmed)::value_type) }));
	return composite;
}

/*
 * class recently_cemented
 */

nano::recently_cemented_cache::recently_cemented_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void nano::recently_cemented_cache::put (const nano::election_status & status)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	cemented.push_back (status);
	if (cemented.size () > max_size)
	{
		cemented.pop_front ();
	}
}

nano::recently_cemented_cache::queue_t nano::recently_cemented_cache::list () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return cemented;
}

std::size_t nano::recently_cemented_cache::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return cemented.size ();
}

std::unique_ptr<nano::container_info_component> nano::recently_cemented_cache::collect_container_info (const std::string & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cemented", cemented.size (), sizeof (decltype (cemented)::value_type) }));
	return composite;
}
