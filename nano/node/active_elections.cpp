#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/component.hpp>

#include <ranges>

using namespace std::chrono;

nano::active_elections::active_elections (nano::node & node_a, nano::confirming_set & confirming_set_a, nano::block_processor & block_processor_a) :
	config{ node_a.config.active_elections },
	node{ node_a },
	confirming_set{ confirming_set_a },
	block_processor{ block_processor_a },
	recently_confirmed{ config.confirmation_cache },
	recently_cemented{ config.confirmation_history_size }
{
	count_by_behavior.fill (0); // Zero initialize array

	confirming_set.batch_cemented.add ([this] (auto const & cemented) {
		auto transaction = node.ledger.tx_begin_read ();
		for (auto const & [block, confirmation_root, source_election] : cemented)
		{
			transaction.refresh_if_needed ();
			block_cemented (transaction, block, confirmation_root, source_election);
		}
	});

	// Notify elections about alternative (forked) blocks
	block_processor.block_processed.add ([this] (auto const & result, auto const & context) {
		if (result == nano::block_status::fork)
		{
			publish (context.block);
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

void nano::active_elections::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
	clear ();
}

void nano::active_elections::block_cemented (nano::secure::transaction const & transaction, std::shared_ptr<nano::block> const & block, nano::block_hash const & confirmation_root, std::shared_ptr<nano::election> const & source_election)
{
	debug_assert (node.block_confirmed (block->hash ()));

	// Dependent elections are implicitly confirmed when their block is cemented
	auto dependend_election = election (block->qualified_root ());
	if (dependend_election)
	{
		node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::confirm_dependent);
		dependend_election->try_confirm (block->hash ());
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

	notify_observers (transaction, status, votes);

	bool cemented_bootstrap_count_reached = node.ledger.cemented_count () >= node.ledger.bootstrap_weight_max_blocks;
	bool was_active = status.type == nano::election_status_type::active_confirmed_quorum || status.type == nano::election_status_type::active_confirmation_height;

	// Next-block activations are only done for blocks with previously active elections
	if (cemented_bootstrap_count_reached && was_active && !node.flags.disable_activate_successors)
	{
		activate_successors (transaction, block);
	}
}

void nano::active_elections::notify_observers (nano::secure::transaction const & transaction, nano::election_status const & status, std::vector<nano::vote_with_weight_info> const & votes) const
{
	auto block = status.winner;
	auto account = block->account ();

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

void nano::active_elections::activate_successors (nano::secure::transaction const & transaction, std::shared_ptr<nano::block> const & block)
{
	node.scheduler.priority.activate (transaction, block->account ());

	// Start or vote for the next unconfirmed block in the destination account
	if (block->is_send () && !block->destination ().is_zero () && block->destination () != block->account ())
	{
		node.scheduler.priority.activate (transaction, block->destination ());
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
		return static_cast<int64_t> (config.max_election_winners) - static_cast<int64_t> (confirming_set.size ());
	};

	return std::min (election_vacancy (behavior), election_winners_vacancy ());
}

void nano::active_elections::request_confirm (nano::unique_lock<nano::mutex> & lock_a)
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

void nano::active_elections::cleanup_election (nano::unique_lock<nano::mutex> & lock_a, std::shared_ptr<nano::election> election)
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

	node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::stopped);
	node.stats.inc (nano::stat::type::active_elections, election->confirmed () ? nano::stat::detail::confirmed : nano::stat::detail::unconfirmed);
	node.stats.inc (nano::stat::type::active_elections_stopped, to_stat_detail (election->state ()));
	node.stats.inc (to_stat_type (election->state ()), to_stat_detail (election->behavior ()));

	node.logger.trace (nano::log::type::active_elections, nano::log::detail::active_stopped, nano::log::arg{ "election", election });

	node.logger.debug (nano::log::type::active_elections, "Erased election for blocks: {} (behavior: {}, state: {})",
	fmt::join (std::views::keys (blocks_l), ", "),
	to_string (election->behavior ()),
	to_string (election->state ()));

	lock_a.unlock ();

	// Track election duration
	node.stats.sample (nano::stat::sample::active_election_duration, election->duration ().count (), { 0, 1000 * 60 * 10 /* 0-10 minutes range */ });

	// Notify observers without holding the lock
	if (entry.erased_callback)
	{
		entry.erased_callback (election);
	}

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
			node.network.filter.clear (block);
		}
	}
}

std::vector<std::shared_ptr<nano::election>> nano::active_elections::list_active (std::size_t max_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return list_active_impl (max_a);
}

std::vector<std::shared_ptr<nano::election>> nano::active_elections::list_active_impl (std::size_t max_a) const
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

void nano::active_elections::request_loop ()
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

nano::election_insertion_result nano::active_elections::insert (std::shared_ptr<nano::block> const & block_a, nano::election_behavior election_behavior_a, erased_callback_t erased_callback_a)
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
			roots.get<tag_root> ().emplace (entry{ root, result.election, std::move (erased_callback_a) });
			node.vote_router.connect (hash, result.election);

			// Keep track of election count by election type
			debug_assert (count_by_behavior[result.election->behavior ()] >= 0);
			count_by_behavior[result.election->behavior ()]++;

			node.stats.inc (nano::stat::type::active_elections, nano::stat::detail::started);
			node.stats.inc (nano::stat::type::active_elections_started, to_stat_detail (election_behavior_a));

			node.logger.trace (nano::log::type::active_elections, nano::log::detail::active_started,
			nano::log::arg{ "behavior", election_behavior_a },
			nano::log::arg{ "election", result.election });

			node.logger.debug (nano::log::type::active_elections, "Started new election for block: {} (behavior: {})",
			hash.to_string (),
			to_string (election_behavior_a));
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

		node.vote_cache_processor.trigger (hash);
		node.observers.active_started.notify (hash);
		vacancy_update ();
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

std::shared_ptr<nano::election> nano::active_elections::election (nano::qualified_root const & root_a) const
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
			node.logger.debug (nano::log::type::active_elections, "Block was added to an existing election: {}", block_a->hash ().to_string ());
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

	vacancy_update ();
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

	return toml.get_error ();
}

nano::error nano::active_elections_config::deserialize (nano::tomlconfig & toml)
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