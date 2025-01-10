#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/confirmation_solicitor.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/repcrawler.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/store/component.hpp>

#include <ranges>

using namespace std::chrono;

celerix::active_elections::active_elections (celerix::node & node_a, celerix::confirming_set & confirming_set_a, celerix::block_processor & block_processor_a) :
	config{ node_a.config.active_elections },
	node{ node_a },
	confirming_set{ confirming_set_a },
	block_processor{ block_processor_a },
	recently_confirmed{ config.confirmation_cache },
	recently_cemented{ config.confirmation_history_size }
{
	count_by_behavior.fill (0); // Zero initialize array

	// Cementing blocks might implicitly confirm dependent elections
	confirming_set.batch_cemented.add ([this] (auto const & cemented) {
		std::deque<block_cemented_result> results;
		{
			// Process all cemented blocks while holding the lock to avoid races where an election for a block that is already cemented is inserted
			celerix::lock_guard<celerix::mutex> guard{ mutex };
			for (auto const & [block, confirmation_root, source_election] : cemented)
			{
				auto result = block_cemented (block, confirmation_root, source_election);
				results.push_back (result);
			}
		}
		{
			// TODO: This could be offloaded to a separate notification worker, profiling is needed
			auto transaction = node.ledger.tx_begin_read ();
			for (auto const & [status, votes] : results)
			{
				transaction.refresh_if_needed ();
				notify_observers (transaction, status, votes);
			}
		}
	});

	// Notify elections about alternative (forked) blocks
	block_processor.batch_processed.add ([this] (auto const & batch) {
		for (auto const & [result, context] : batch)
		{
			if (result == celerix::block_status::fork)
			{
				publish (context.block);
			}
		}
	});

	// Stop all rolled back active transactions except initial
	block_processor.rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		for (auto const & block : blocks)
		{
			if (block->qualified_root () != rollback_root)
			{
				erase (block->qualified_root ());
			}
		}
	});
}

celerix::active_elections::~active_elections ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::active_elections::start ()
{
	if (node.flags.disable_request_loop)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::request_loop);
		request_loop ();
	});
}

void celerix::active_elections::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	celerix::join_or_pass (thread);
	clear ();
}

auto celerix::active_elections::block_cemented (std::shared_ptr<celerix::block> const & block, celerix::block_hash const & confirmation_root, std::shared_ptr<celerix::election> const & source_election) -> block_cemented_result
{
	debug_assert (!mutex.try_lock ());
	debug_assert (node.block_confirmed (block->hash ()));

	// Dependent elections are implicitly confirmed when their block is cemented
	auto dependend_election = election_impl (block->qualified_root ());
	if (dependend_election)
	{
		node.stats.inc (celerix::stat::type::active_elections, celerix::stat::detail::confirm_dependent);
		dependend_election->try_confirm (block->hash ()); // TODO: This should either confirm or cancel the election
	}

	celerix::election_status status;
	std::vector<celerix::vote_with_weight_info> votes;
	status.winner = block;

	// Check if the currently cemented block was part of an election that triggered the confirmation
	if (source_election && source_election->qualified_root == block->qualified_root ())
	{
		status = source_election->get_status ();
		debug_assert (status.winner->hash () == block->hash ());
		votes = source_election->votes_with_weight ();
		status.type = celerix::election_status_type::active_confirmed_quorum;
	}
	else if (dependend_election)
	{
		status.type = celerix::election_status_type::active_confirmation_height;
	}
	else
	{
		status.type = celerix::election_status_type::inactive_confirmation_height;
	}

	recently_cemented.put (status);

	node.stats.inc (celerix::stat::type::active_elections, celerix::stat::detail::cemented);
	node.stats.inc (celerix::stat::type::active_elections_cemented, to_stat_detail (status.type));

	node.logger.trace (celerix::log::type::active_elections, celerix::log::detail::active_cemented,
	celerix::log::arg{ "block", block },
	celerix::log::arg{ "confirmation_root", confirmation_root },
	celerix::log::arg{ "source_election", source_election });

	return { status, votes };
}

void celerix::active_elections::notify_observers (celerix::secure::transaction const & transaction, celerix::election_status const & status, std::vector<celerix::vote_with_weight_info> const & votes) const
{
	auto block = status.winner;
	auto account = block->account ();

	switch (status.type)
	{
		case celerix::election_status_type::active_confirmed_quorum:
			node.stats.inc (celerix::stat::type::confirmation_observer, celerix::stat::detail::active_quorum, celerix::stat::dir::out);
			break;
		case celerix::election_status_type::active_confirmation_height:
			node.stats.inc (celerix::stat::type::confirmation_observer, celerix::stat::detail::active_conf_height, celerix::stat::dir::out);
			break;
		case celerix::election_status_type::inactive_confirmation_height:
			node.stats.inc (celerix::stat::type::confirmation_observer, celerix::stat::detail::inactive_conf_height, celerix::stat::dir::out);
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

int64_t celerix::active_elections::limit (celerix::election_behavior behavior) const
{
	switch (behavior)
	{
		case celerix::election_behavior::manual:
		{
			return std::numeric_limits<int64_t>::max ();
		}
		case celerix::election_behavior::priority:
		{
			return static_cast<int64_t> (config.size);
		}
		case celerix::election_behavior::hinted:
		{
			const uint64_t limit = config.hinted_limit_percentage * config.size / 100;
			return static_cast<int64_t> (limit);
		}
		case celerix::election_behavior::optimistic:
		{
			const uint64_t limit = config.optimistic_limit_percentage * config.size / 100;
			return static_cast<int64_t> (limit);
		}
	}

	debug_assert (false, "unknown election behavior");
	return 0;
}

int64_t celerix::active_elections::vacancy (celerix::election_behavior behavior) const
{
	auto election_vacancy = [this] (celerix::election_behavior behavior) -> int64_t {
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		switch (behavior)
		{
			case celerix::election_behavior::manual:
				return std::numeric_limits<int64_t>::max ();
			case celerix::election_behavior::priority:
				return limit (celerix::election_behavior::priority) - static_cast<int64_t> (roots.size ());
			case celerix::election_behavior::hinted:
			case celerix::election_behavior::optimistic:
				return limit (behavior) - count_by_behavior[behavior];
		}
		debug_assert (false); // Unknown enum
		return 0;
	};

	auto election_winners_vacancy = [this] () -> int64_t {
		return static_cast<int64_t> (config.max_election_winners) - static_cast<int64_t> (confirming_set.size ());
	};

	return std::min (election_vacancy (behavior), election_winners_vacancy ());
}

void celerix::active_elections::request_confirm (celerix::unique_lock<celerix::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	std::size_t const this_loop_target_l (roots.size ());
	auto const elections_l{ list_active_impl (this_loop_target_l) };

	lock_a.unlock ();

	celerix::confirmation_solicitor solicitor (node.network, node.config);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<std::size_t>::max ()));

	std::size_t unconfirmed_count_l (0);
	celerix::timer<std::chrono::milliseconds> elapsed (celerix::timer_state::started);

	/*
	 * Loop through active elections in descending order of proof-of-work difficulty, requesting confirmation
	 *
	 * Only up to a certain amount of elections are queued for confirmation request and block rebroadcasting. The remaining elections can still be confirmed if votes arrive
	 * Elections extending the soft config.size limit are flushed after a certain time-to-live cutoff
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

void celerix::active_elections::cleanup_election (celerix::unique_lock<celerix::mutex> & lock_a, std::shared_ptr<celerix::election> election)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock_a.owns_lock ());
	debug_assert (!election->confirmed () || recently_confirmed.exists (election->qualified_root));

	// Keep track of election count by election type
	debug_assert (count_by_behavior[election->behavior ()] > 0);
	count_by_behavior[election->behavior ()]--;

	auto blocks_l = election->blocks ();
	node.vote_router.disconnect (*election);

	// Erase root info
	auto it = roots.get<tag_root> ().find (election->qualified_root);
	release_assert (it != roots.get<tag_root> ().end ());
	entry entry = *it;
	roots.get<tag_root> ().erase (it);

	node.stats.inc (celerix::stat::type::active_elections, celerix::stat::detail::stopped);
	node.stats.inc (celerix::stat::type::active_elections, election->confirmed () ? celerix::stat::detail::confirmed : celerix::stat::detail::unconfirmed);
	node.stats.inc (celerix::stat::type::active_elections_stopped, to_stat_detail (election->state ()));
	node.stats.inc (to_stat_type (election->state ()), to_stat_detail (election->behavior ()));

	node.logger.trace (celerix::log::type::active_elections, celerix::log::detail::active_stopped, celerix::log::arg{ "election", election });

	node.logger.debug (celerix::log::type::active_elections, "Erased election for blocks: {} (behavior: {}, state: {})",
	fmt::join (std::views::keys (blocks_l), ", "),
	to_string (election->behavior ()),
	to_string (election->state ()));

	lock_a.unlock ();

	// Track election duration
	node.stats.sample (celerix::stat::sample::active_election_duration, election->duration ().count (), { 0, 1000 * 60 * 10 /* 0-10 minutes range */ });

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

std::vector<std::shared_ptr<celerix::election>> celerix::active_elections::list_active (std::size_t max_a)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return list_active_impl (max_a);
}

std::vector<std::shared_ptr<celerix::election>> celerix::active_elections::list_active_impl (std::size_t max_a) const
{
	std::vector<std::shared_ptr<celerix::election>> result_l;
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

void celerix::active_elections::request_loop ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		auto const stamp_l = std::chrono::steady_clock::now ();

		node.stats.inc (celerix::stat::type::active, celerix::stat::detail::loop);

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

celerix::election_insertion_result celerix::active_elections::insert (std::shared_ptr<celerix::block> const & block_a, celerix::election_behavior election_behavior_a, erased_callback_t erased_callback_a)
{
	debug_assert (block_a);
	debug_assert (block_a->has_sideband ());

	celerix::unique_lock<celerix::mutex> lock{ mutex };

	celerix::election_insertion_result result;

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
				// TODO: Is this neccessary? Move this outside of the election class
				// Representative is defined as online if replying to live votes or rep_crawler queries
				node.online_reps.observe (rep_a);
			};
			result.election = celerix::make_shared<celerix::election> (node, block_a, nullptr, observe_rep_cb, election_behavior_a);
			roots.get<tag_root> ().emplace (entry{ root, result.election, std::move (erased_callback_a) });
			node.vote_router.connect (hash, result.election);

			// Keep track of election count by election type
			debug_assert (count_by_behavior[result.election->behavior ()] >= 0);
			count_by_behavior[result.election->behavior ()]++;

			// Skip passive phase for blocks without cached votes to avoid bootstrap delays
			bool active_immediately = false;
			if (node.vote_cache.contains (hash))
			{
				result.election->transition_active ();
				active_immediately = true;
			}

			node.stats.inc (celerix::stat::type::active_elections, celerix::stat::detail::started);
			node.stats.inc (celerix::stat::type::active_elections_started, to_stat_detail (election_behavior_a));

			node.logger.trace (celerix::log::type::active_elections, celerix::log::detail::active_started,
			celerix::log::arg{ "behavior", election_behavior_a },
			celerix::log::arg{ "election", result.election });

			node.logger.debug (celerix::log::type::active_elections, "Started new election for block: {} (behavior: {}, active immediately: {})",
			hash.to_string (),
			to_string (election_behavior_a),
			active_immediately);
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
		if (election_behavior_a == celerix::election_behavior::priority && previous_behavior != celerix::election_behavior::priority)
		{
			bool transitioned = result.election->transition_priority ();
			if (transitioned)
			{
				count_by_behavior[previous_behavior]--;
				count_by_behavior[election_behavior_a]++;
				node.stats.inc (celerix::stat::type::active_elections, celerix::stat::detail::transition_priority);
			}
			else
			{
				node.stats.inc (celerix::stat::type::active_elections, celerix::stat::detail::transition_priority_failed);
			}
		}
	}

	lock.unlock ();

	if (result.inserted)
	{
		debug_assert (result.election);

		node.vote_cache_processor.trigger (hash);
		node.observers.active_started.notify (hash);
		vacancy_updated.notify ();
	}

	// Votes are generated for inserted or ongoing elections
	if (result.election)
	{
		result.election->broadcast_vote ();
	}

	return result;
}

bool celerix::active_elections::active (celerix::qualified_root const & root_a) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return roots.get<tag_root> ().find (root_a) != roots.get<tag_root> ().end ();
}

bool celerix::active_elections::active (celerix::block const & block_a) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return roots.get<tag_root> ().find (block_a.qualified_root ()) != roots.get<tag_root> ().end ();
}

std::shared_ptr<celerix::election> celerix::active_elections::election (celerix::qualified_root const & root) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return election_impl (root);
}

std::shared_ptr<celerix::election> celerix::active_elections::election_impl (celerix::qualified_root const & root) const
{
	debug_assert (!mutex.try_lock ());
	std::shared_ptr<celerix::election> result;
	auto existing = roots.get<tag_root> ().find (root);
	if (existing != roots.get<tag_root> ().end ())
	{
		result = existing->election;
	}
	return result;
}

bool celerix::active_elections::erase (celerix::block const & block_a)
{
	return erase (block_a.qualified_root ());
}

bool celerix::active_elections::erase (celerix::qualified_root const & root_a)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	auto root_it (roots.get<tag_root> ().find (root_a));
	if (root_it != roots.get<tag_root> ().end ())
	{
		release_assert (root_it->election->qualified_root == root_a);
		cleanup_election (lock, root_it->election);
		return true;
	}
	return false;
}

bool celerix::active_elections::empty () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return roots.empty ();
}

std::size_t celerix::active_elections::size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return roots.size ();
}

std::size_t celerix::active_elections::size (celerix::election_behavior behavior) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto count = count_by_behavior[behavior];
	debug_assert (count >= 0);
	return static_cast<std::size_t> (count);
}

bool celerix::active_elections::publish (std::shared_ptr<celerix::block> const & block_a)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
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

			node.stats.inc (celerix::stat::type::active, celerix::stat::detail::election_block_conflict);
			node.logger.debug (celerix::log::type::active_elections, "Block was added to an existing election: {}", block_a->hash ().to_string ());
		}
	}
	return result;
}

void celerix::active_elections::clear ()
{
	// TODO: Call erased_callback for each election
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		roots.clear ();
	}
	vacancy_updated.notify ();
}

celerix::container_info celerix::active_elections::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("roots", roots.size ());
	info.put ("normal", static_cast<std::size_t> (count_by_behavior[celerix::election_behavior::priority]));
	info.put ("hinted", static_cast<std::size_t> (count_by_behavior[celerix::election_behavior::hinted]));
	info.put ("optimistic", static_cast<std::size_t> (count_by_behavior[celerix::election_behavior::optimistic]));

	info.add ("recently_confirmed", recently_confirmed.container_info ());
	info.add ("recently_cemented", recently_cemented.container_info ());

	return info;
}

/*
 * active_elections_config
 */

celerix::active_elections_config::active_elections_config (const celerix::network_constants & network_constants)
{
}

celerix::error celerix::active_elections_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("size", size, "Number of active elections. Elections beyond this limit have limited survival time.\nWarning: modifying this value may result in a lower confirmation rate. \ntype:uint64,[250..]");
	toml.put ("hinted_limit_percentage", hinted_limit_percentage, "Limit of hinted elections as percentage of `active_elections_size` \ntype:uint64");
	toml.put ("optimistic_limit_percentage", optimistic_limit_percentage, "Limit of optimistic elections as percentage of `active_elections_size`. \ntype:uint64");
	toml.put ("confirmation_history_size", confirmation_history_size, "Maximum confirmation history size. If tracking the rate of block confirmations, the websocket feature is recommended instead. \ntype:uint64");
	toml.put ("confirmation_cache", confirmation_cache, "Maximum number of confirmed elections kept in cache to prevent restarting an election. \ntype:uint64");

	return toml.get_error ();
}

celerix::error celerix::active_elections_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("size", size);
	toml.get ("hinted_limit_percentage", hinted_limit_percentage);
	toml.get ("optimistic_limit_percentage", optimistic_limit_percentage);
	toml.get ("confirmation_history_size", confirmation_history_size);
	toml.get ("confirmation_cache", confirmation_cache);

	return toml.get_error ();
}

/*
 *
 */

celerix::stat::type celerix::to_stat_type (celerix::election_state state)
{
	switch (state)
	{
		case election_state::passive:
		case election_state::active:
			return celerix::stat::type::active_elections_dropped;
			break;
		case election_state::confirmed:
		case election_state::expired_confirmed:
			return celerix::stat::type::active_elections_confirmed;
			break;
		case election_state::expired_unconfirmed:
			return celerix::stat::type::active_elections_timeout;
			break;
		case election_state::cancelled:
			return celerix::stat::type::active_elections_cancelled;
			break;
	}
	debug_assert (false);
	return {};
}

celerix::stat::detail celerix::to_stat_detail (celerix::election_status_type type)
{
	return celerix::enum_util::cast<celerix::stat::detail> (type);
}
