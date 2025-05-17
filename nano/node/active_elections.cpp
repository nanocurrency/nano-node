#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/node.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/component.hpp>

#include <ranges>

using namespace std::chrono;

nano::active_elections::active_elections (nano::node & node_a, nano::ledger_notifications & ledger_notifications_a, nano::cementing_set & cementing_set_a) :
	config{ node_a.config.active_elections },
	node{ node_a },
	ledger_notifications{ ledger_notifications_a },
	cementing_set{ cementing_set_a },
	recently_confirmed{ config.confirmation_cache },
	recently_cemented{ config.confirmation_history_size },
	workers{ 1, nano::thread_role::name::aec_notifications }
{
	count_by_behavior.fill (0); // Zero initialize array

	// Cementing blocks might implicitly confirm dependent elections
	cementing_set.batch_cemented.add ([this] (auto const & cemented) {
		std::deque<block_cemented_result> results;
		{
			// Process all cemented blocks while holding the lock to avoid races where an election for a block that is already cemented is inserted
			nano::lock_guard<nano::mutex> guard{ mutex };
			for (auto const & [block, confirmation_root, source_election] : cemented)
			{
				auto result = block_cemented (block, confirmation_root, source_election);
				results.push_back (result);
			}
		}

		// Notify observers about cemented blocks on a background thread
		workers.post ([this, results = std::move (results)] () {
			auto transaction = node.ledger.tx_begin_read ();
			for (auto const & [status, votes] : results)
			{
				transaction.refresh_if_needed ();
				notify_observers (transaction, status, votes);
			}
		});
	});

	// Notify elections about alternative (forked) blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		for (auto const & [result, context] : batch)
		{
			if (result == nano::block_status::fork)
			{
				publish (context.block);
			}
		}
	});

	// Stop all rolled back active transactions except initial
	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		for (auto const & block : blocks)
		{
			if (block->qualified_root () != rollback_root)
			{
				erase (block->qualified_root ());
			}
		}
	});
}

nano::active_elections::~active_elections ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::active_elections::start ()
{
	workers.start ();

	if (node.flags.disable_request_loop)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::aec_loop);
		run ();
	});
}

void nano::active_elections::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	workers.stop ();

	clear ();
}

void nano::active_elections::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto const stamp = std::chrono::steady_clock::now ();

		node.stats.inc (nano::stat::type::active, nano::stat::detail::loop);

		tick_elections (lock);
		debug_assert (!lock.owns_lock ());
		lock.lock ();

		auto const min_sleep = node.network_params.network.aec_loop_interval / 2;
		auto const wakeup = std::max (stamp + node.network_params.network.aec_loop_interval, std::chrono::steady_clock::now () + min_sleep);

		condition.wait_until (lock, wakeup, [this, wakeup] {
			return stopped || std::chrono::steady_clock::now () >= wakeup;
		});
	}
}

auto nano::active_elections::block_cemented (std::shared_ptr<nano::block> const & block, nano::block_hash const & confirmation_root, std::shared_ptr<nano::election> const & source_election) -> block_cemented_result
{
	debug_assert (!mutex.try_lock ());
	debug_assert (node.block_confirmed (block->hash ()));

	// Dependent elections are implicitly confirmed when their block is cemented
	auto dependend_election = election_impl (block->qualified_root ());
	if (dependend_election)
	{
		node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::confirm_dependent);
		dependend_election->try_confirm (block->hash ()); // TODO: This should either confirm or cancel the election
	}

	nano::election_status status;
	std::vector<nano::vote_with_weight_info> votes;
	status.winner = block;

	// Check if the currently cemented block was part of an election that triggered the confirmation
	if (source_election && source_election->qualified_root == block->qualified_root ())
	{
		status = source_election->get_status ();
		debug_assert (status.winner->hash () == block->hash ());
		votes = source_election->votes_with_weight ();
		status.type = nano::election_status_type::active_confirmed_quorum;
	}
	else if (dependend_election)
	{
		status.type = nano::election_status_type::active_confirmation_height;
	}
	else
	{
		status.type = nano::election_status_type::inactive_confirmation_height;
	}

	recently_cemented.put (status);

	node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::cemented);
	node.stats.inc (nano::stat::type::active_elections_cemented, to_stat_detail (status.type));

	node.logger.trace (nano::log::type::active_elections, nano::log::detail::active_cemented,
	nano::log::arg{ "block", block },
	nano::log::arg{ "confirmation_root", confirmation_root },
	nano::log::arg{ "source_election", source_election });

	return { status, votes };
}

void nano::active_elections::notify_observers (nano::secure::transaction const & transaction, nano::election_status const & status, std::vector<nano::vote_with_weight_info> const & votes) const
{
	// Get block from ledger to ensure sideband is set (forked blocks may not have sideband)
	auto const block = node.ledger.any.block_get (transaction, status.winner->hash ());
	release_assert (block != nullptr); // Block must exist in the ledger since it was cemented
	auto const account = block->account ();

	switch (status.type)
	{
		case nano::election_status_type::active_confirmed_quorum:
			node.stats.inc (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out);
			break;
		case nano::election_status_type::active_confirmation_height:
			node.stats.inc (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out);
			break;
		case nano::election_status_type::inactive_confirmation_height:
			node.stats.inc (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out);
			break;
		default:
			break;
	}

	if (!node.observers.blocks.empty ())
	{
		auto amount = node.ledger.any.block_amount (transaction, block).value_or (0).number ();
		auto is_state_send = block->type () == block_type::state && block->is_send ();
		auto is_state_epoch = block->type () == block_type::state && block->is_epoch ();
		node.observers.blocks.notify (status, votes, account, amount, is_state_send, is_state_epoch);
	}

	node.observers.account_balance.notify (account, false);

	if (block->is_send ())
	{
		node.observers.account_balance.notify (block->destination (), true);
	}
}

int64_t nano::active_elections::limit (nano::election_behavior behavior) const
{
	switch (behavior)
	{
		case nano::election_behavior::manual:
		{
			return std::numeric_limits<int64_t>::max ();
		}
		case nano::election_behavior::priority:
		{
			return static_cast<int64_t> (config.size);
		}
		case nano::election_behavior::hinted:
		{
			const uint64_t limit = config.hinted_limit_percentage * config.size / 100;
			return static_cast<int64_t> (limit);
		}
		case nano::election_behavior::optimistic:
		{
			const uint64_t limit = config.optimistic_limit_percentage * config.size / 100;
			return static_cast<int64_t> (limit);
		}
	}

	debug_assert (false, "unknown election behavior");
	return 0;
}

int64_t nano::active_elections::vacancy (nano::election_behavior behavior) const
{
	auto election_vacancy = [this] (nano::election_behavior behavior) -> int64_t {
		nano::lock_guard<nano::mutex> guard{ mutex };
		switch (behavior)
		{
			case nano::election_behavior::manual:
				return std::numeric_limits<int64_t>::max ();
			case nano::election_behavior::priority:
				return limit (nano::election_behavior::priority) - static_cast<int64_t> (roots.size ());
			case nano::election_behavior::hinted:
			case nano::election_behavior::optimistic:
				return limit (behavior) - count_by_behavior[behavior];
		}
		debug_assert (false); // Unknown enum
		return 0;
	};

	auto election_winners_vacancy = [this] () -> int64_t {
		return static_cast<int64_t> (config.max_election_winners) - static_cast<int64_t> (cementing_set.size ());
	};

	return std::min (election_vacancy (behavior), election_winners_vacancy ());
}

void nano::active_elections::tick_elections (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	auto const election_list = list_active_impl ();

	lock.unlock ();

	nano::confirmation_solicitor solicitor (node.network, node.config);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<std::size_t>::max ()));

	nano::timer<std::chrono::milliseconds> elapsed (nano::timer_state::started);

	std::deque<std::shared_ptr<nano::election>> stale_elections;
	for (auto const & election : election_list)
	{
		if (election->transition_time (solicitor))
		{
			erase (election->qualified_root);
		}
		else if (election->duration () > config.bootstrap_stale_threshold)
		{
			stale_elections.push_back (election);
		}
	}

	solicitor.flush ();

	if (bootstrap_stale_interval.elapse (config.bootstrap_stale_threshold / 2))
	{
		node.stats.add (nano::stat::type::active_elections, nano::stat::detail::bootstrap_stale, stale_elections.size ());

		for (auto const & election : stale_elections)
		{
			node.logger.debug (nano::log::type::active_elections, "Bootstrapping account: {} with stale election with root: {}, blocks: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
			election->account,
			election->qualified_root,
			fmt::join (election->blocks_hashes (), ", "), // TODO: Lazy eval
			to_string (election->behavior ()),
			to_string (election->state ()),
			election->voter_count (),
			election->block_count (),
			election->duration ().count ());

			node.bootstrap.prioritize (election->account);
		}
	}
}

void nano::active_elections::cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election> election)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock_a.owns_lock ());
	debug_assert (!election->confirmed () || recently_confirmed.exists (election->qualified_root));

	// Keep track of election count by election type
	release_assert (count_by_behavior[election->behavior ()] > 0);
	count_by_behavior[election->behavior ()]--;

	auto blocks_l = election->blocks ();
	node.vote_router.disconnect (*election);

	// Erase root info
	auto it = roots.get<tag_root> ().find (election->qualified_root);
	release_assert (it != roots.get<tag_root> ().end ());
	entry entry = *it;
	roots.get<tag_root> ().erase (it);

	node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::stopped);
	node.stats.inc (nano::stat::type::active_elections, election->confirmed () ? nano::stat::detail::confirmed : nano::stat::detail::unconfirmed);
	node.stats.inc (nano::stat::type::active_elections_stopped, to_stat_detail (election->state ()));
	node.stats.inc (to_stat_type (election->state ()), to_stat_detail (election->behavior ()));

	node.logger.trace (nano::log::type::active_elections, nano::log::detail::active_stopped, nano::log::arg{ "election", election });

	node.logger.debug (nano::log::type::active_elections, "Erased election for root: {} with blocks: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
	election->qualified_root,
	fmt::join (election->blocks_hashes (), ", "), // TODO: Lazy eval
	to_string (election->behavior ()),
	to_string (election->state ()),
	election->voter_count (),
	election->block_count (),
	election->duration ().count ());

	lock_a.unlock ();

	// Track election duration
	node.stats.sample (nano::stat::sample::active_election_duration, election->duration ().count (), { 0, 1000 * 60 * 10 /* 0-10 minutes range */ });

	// Notify observers without holding the lock
	if (entry.erased_callback)
	{
		entry.erased_callback (election);
	}

	vacancy_updated.notify ();

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
			node.network.filter.clear (block);
		}
	}
}

std::vector<std::shared_ptr<nano::election>> nano::active_elections::list_active (std::size_t max_count)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return list_active_impl (max_count);
}

std::vector<std::shared_ptr<nano::election>> nano::active_elections::list_active_impl (std::size_t max_count) const
{
	std::vector<std::shared_ptr<nano::election>> result_l;
	result_l.reserve (std::min (max_count, roots.size ()));
	{
		auto & sorted_roots_l (roots.get<tag_sequenced> ());
		for (auto i = sorted_roots_l.begin (), n = sorted_roots_l.end (); i != n && result_l.size () < max_count; ++i)
		{
			result_l.push_back (i->election);
		}
	}
	return result_l;
}

nano::election_insertion_result nano::active_elections::insert (std::shared_ptr<nano::block> const & block_a, nano::election_behavior election_behavior_a, erased_callback_t erased_callback_a)
{
	release_assert (block_a);
	release_assert (block_a->has_sideband ());

	nano::unique_lock<nano::mutex> lock{ mutex };

	nano::election_insertion_result result;

	if (stopped)
	{
		return result;
	}

	auto const root = block_a->qualified_root ();
	auto const hash = block_a->hash ();

	if (auto existing = roots.get<tag_root> ().find (root); existing == roots.get<tag_root> ().end ())
	{
		if (!recently_confirmed.exists (root))
		{
			result.inserted = true;

			// Passing this callback into the election is important
			// We need to observe and update the online voting weight *before* election quorum is checked
			auto observe_rep_callback = [&node = node] (auto const & rep_a) {
				node.online_reps.observe (rep_a);
			};
			result.election = nano::make_shared<nano::election> (node, block_a, nullptr, observe_rep_callback, election_behavior_a);

			roots.get<tag_root> ().emplace (entry{ root, result.election, std::move (erased_callback_a) });
			node.vote_router.connect (hash, result.election);

			// Keep track of election count by election type
			release_assert (count_by_behavior[result.election->behavior ()] >= 0);
			count_by_behavior[result.election->behavior ()]++;

			// Skip passive phase for blocks without cached votes to avoid bootstrap delays
			bool activate_immediately = false;
			if (!node.vote_cache.contains (hash))
			{
				activate_immediately = true;
			}

			if (activate_immediately)
			{
				node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::activate_immediately);
				result.election->transition_active ();
			}

			node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::started);
			node.stats.inc (nano::stat::type::active_elections_started, to_stat_detail (election_behavior_a));

			node.logger.trace (nano::log::type::active_elections, nano::log::detail::active_started,
			nano::log::arg{ "behavior", election_behavior_a },
			nano::log::arg{ "election", result.election });

			node.logger.debug (nano::log::type::active_elections, "Started new election for root: {} with blocks: {} (behavior: {}, active immediately: {})",
			root,
			fmt::join (result.election->blocks_hashes (), ", "), // TODO: Lazy eval
			to_string (election_behavior_a),
			activate_immediately);
		}
		else
		{
			// result is not set
		}
	}
	else
	{
		result.election = existing->election;

		// Upgrade to priority election to enable immediate vote broadcasting.
		auto previous_behavior = result.election->behavior ();
		if (election_behavior_a == nano::election_behavior::priority && previous_behavior != nano::election_behavior::priority)
		{
			bool transitioned = result.election->transition_priority ();
			if (transitioned)
			{
				count_by_behavior[previous_behavior]--;
				count_by_behavior[election_behavior_a]++;
				node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::transition_priority);
			}
			else
			{
				node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::transition_priority_failed);
			}
		}
	}

	lock.unlock ();

	if (result.inserted)
	{
		release_assert (result.election);

		// Notifications
		node.observers.active_started.notify (hash);
		vacancy_updated.notify ();

		// Let the election know about already observed votes
		node.vote_cache_processor.trigger (hash);

		// Let the election know about already observed forks
		auto forks = node.fork_cache.get (root);
		node.stats.add (nano::stat::type::active_elections, nano::stat::detail::forks_cached, forks.size ());

		for (auto const & fork : forks)
		{
			publish (fork);
		}
	}

	// Votes are generated for inserted or ongoing elections
	if (result.election)
	{
		result.election->broadcast_vote ();
	}

	return result;
}

bool nano::active_elections::active (nano::qualified_root const & root_a) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return roots.get<tag_root> ().find (root_a) != roots.get<tag_root> ().end ();
}

bool nano::active_elections::active (nano::block const & block_a) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return roots.get<tag_root> ().find (block_a.qualified_root ()) != roots.get<tag_root> ().end ();
}

std::shared_ptr<nano::election> nano::active_elections::election (nano::qualified_root const & root) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return election_impl (root);
}

std::shared_ptr<nano::election> nano::active_elections::election_impl (nano::qualified_root const & root) const
{
	debug_assert (!mutex.try_lock ());
	std::shared_ptr<nano::election> result;
	auto existing = roots.get<tag_root> ().find (root);
	if (existing != roots.get<tag_root> ().end ())
	{
		result = existing->election;
	}
	return result;
}

bool nano::active_elections::erase (nano::block const & block_a)
{
	return erase (block_a.qualified_root ());
}

bool nano::active_elections::erase (nano::qualified_root const & root_a)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto root_it (roots.get<tag_root> ().find (root_a));
	if (root_it != roots.get<tag_root> ().end ())
	{
		release_assert (root_it->election->qualified_root == root_a);
		cleanup_election (lock, root_it->election);
		return true;
	}
	return false;
}

bool nano::active_elections::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return roots.empty ();
}

std::size_t nano::active_elections::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return roots.size ();
}

std::size_t nano::active_elections::size (nano::election_behavior behavior) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto count = count_by_behavior[behavior];
	debug_assert (count >= 0);
	return static_cast<std::size_t> (count);
}

bool nano::active_elections::publish (std::shared_ptr<nano::block> const & block_a)
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
			node.vote_router.connect (block_a->hash (), election);
			lock.unlock ();

			node.vote_cache_processor.trigger (block_a->hash ());

			node.stats.inc (nano::stat::type::active, nano::stat::detail::election_block_conflict);

			node.logger.debug (nano::log::type::active_elections, "Block was added to an existing election: {} with root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
			block_a->hash (),
			election->qualified_root,
			to_string (election->behavior ()),
			to_string (election->state ()),
			election->voter_count (),
			election->block_count (),
			election->duration ().count ());
		}
	}
	return result;
}

void nano::active_elections::clear ()
{
	// TODO: Call erased_callback for each election
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		roots.clear ();
	}
	vacancy_updated.notify ();
}

nano::container_info nano::active_elections::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("roots", roots.size ());
	info.put ("normal", static_cast<std::size_t> (count_by_behavior[nano::election_behavior::priority]));
	info.put ("hinted", static_cast<std::size_t> (count_by_behavior[nano::election_behavior::hinted]));
	info.put ("optimistic", static_cast<std::size_t> (count_by_behavior[nano::election_behavior::optimistic]));

	info.add ("recently_confirmed", recently_confirmed.container_info ());
	info.add ("recently_cemented", recently_cemented.container_info ());
	info.add ("workers", workers.container_info ());

	return info;
}

/*
 * active_elections_config
 */

nano::active_elections_config::active_elections_config (const nano::network_constants & network_constants)
{
}

nano::error nano::active_elections_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("size", size, "Number of active elections. Elections beyond this limit have limited survival time.\nWarning: modifying this value may result in a lower confirmation rate. \ntype:uint64,[250..]");
	toml.put ("hinted_limit_percentage", hinted_limit_percentage, "Limit of hinted elections as percentage of `active_elections_size` \ntype:uint64");
	toml.put ("optimistic_limit_percentage", optimistic_limit_percentage, "Limit of optimistic elections as percentage of `active_elections_size`. \ntype:uint64");
	toml.put ("confirmation_history_size", confirmation_history_size, "Maximum confirmation history size. If tracking the rate of block confirmations, the websocket feature is recommended instead. \ntype:uint64");
	toml.put ("confirmation_cache", confirmation_cache, "Maximum number of confirmed elections kept in cache to prevent restarting an election. \ntype:uint64");
	toml.put ("max_election_winners", max_election_winners, "Maximum size of election winner details set. \ntype:uint64");
	toml.put ("bootstrap_stale_threshold", bootstrap_stale_threshold.count (), "Time after which additional bootstrap attempts are made to find missing blocks for an election. \ntype:seconds");
	return toml.get_error ();
}

nano::error nano::active_elections_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("size", size);
	toml.get ("hinted_limit_percentage", hinted_limit_percentage);
	toml.get ("optimistic_limit_percentage", optimistic_limit_percentage);
	toml.get ("confirmation_history_size", confirmation_history_size);
	toml.get ("confirmation_cache", confirmation_cache);
	toml.get ("max_election_winners", max_election_winners);
	toml.get_duration ("bootstrap_stale_threshold", bootstrap_stale_threshold);

	return toml.get_error ();
}

/*
 *
 */

nano::stat::type nano::to_stat_type (nano::election_state state)
{
	switch (state)
	{
		case election_state::passive:
		case election_state::active:
			return nano::stat::type::active_elections_dropped;
			break;
		case election_state::confirmed:
		case election_state::expired_confirmed:
			return nano::stat::type::active_elections_confirmed;
			break;
		case election_state::expired_unconfirmed:
			return nano::stat::type::active_elections_timeout;
			break;
		case election_state::cancelled:
			return nano::stat::type::active_elections_cancelled;
			break;
	}
	debug_assert (false);
	return {};
}

nano::stat::detail nano::to_stat_detail (nano::election_status_type type)
{
	return nano::enum_util::cast<nano::stat::detail> (type);
}
