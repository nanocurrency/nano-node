#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/messages/common.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/bootstrap_context.hpp>
#include <nano/node/bootstrap/database_strategy.hpp>
#include <nano/node/bootstrap/dependency_strategy.hpp>
#include <nano/node/bootstrap/frontier_strategy.hpp>
#include <nano/node/bootstrap/priority_strategy.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/bootstrap/topo_strategy.hpp>
#include <nano/node/bootstrap/verify.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>

#include <any>

using namespace std::chrono_literals;

namespace nano::bootstrap
{
bootstrap_context::bootstrap_context (nano::node_config const & node_config_a, nano::node & node_a, nano::ledger & ledger_a,
nano::ledger_notifications & ledger_notifications_a, nano::block_processor & block_processor_a, nano::network & network_a, nano::stats & stat_a, nano::logger & logger_a) :
	config{ *node_config_a.bootstrap },
	network_constants{ node_config_a.network_params.network },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	block_processor{ block_processor_a },
	network{ network_a },
	stats{ stat_a },
	logger{ logger_a },
	priority_strat_impl{ std::make_unique<priority_strategy> (*this) },
	priority_strat{ *priority_strat_impl },
	database_strat_impl{ std::make_unique<database_strategy> (*this) },
	database_strat{ *database_strat_impl },
	dependency_strat_impl{ std::make_unique<dependency_strategy> (*this) },
	dependency_strat{ *dependency_strat_impl },
	frontier_strat_impl{ std::make_unique<frontier_strategy> (*this) },
	frontier_strat{ *frontier_strat_impl },
	topo_strat_impl{ std::make_unique<topo_strategy> (*this) },
	topo_strat{ *topo_strat_impl },
	accounts{ config.account_sets, stats },
	database_scan{ ledger },
	frontiers{ config.frontier_scan, stats },
	throttle{ compute_throttle_size () },
	peers{ config },
	priority_limiter{ config.priority_rate_limit },
	database_limiter{ config.database_rate_limit },
	dependency_limiter{ config.dependency_rate_limit },
	frontier_limiter{ config.frontier_rate_limit },
	topo_limiter{ config.topo_rate_limit },
	priority_channel{ node_a.create_null_channel () },
	database_channel{ node_a.create_null_channel () },
	topology_channel{ node_a.create_null_channel () },
	workers{ 1, nano::thread_role::name::bootstrap_worker }
{
	// Inspect all processed blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };

			auto transaction = ledger.tx_begin_read ();
			for (auto const & [result, block_context] : batch)
			{
				inspect (transaction, result, block_context);
			}
		}
		condition.notify_all ();
	});

	// Handle all rolled back blocks
	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			for (auto const & block : blocks)
			{
				debug_assert (block != nullptr);
				rollback (*block);
			}
		}
		condition.notify_all ();
	});

	accounts.priority_set (node_config_a.network_params.ledger.genesis->account_field ().value ());
}

bootstrap_context::~bootstrap_context ()
{
	// All threads must be stopped before destruction
	debug_assert (!maintenance_thread.joinable ());
	debug_assert (!workers.alive ());
}

void bootstrap_context::start ()
{
	debug_assert (!maintenance_thread.joinable ());

	if (!config.enable)
	{
		logger.warn (nano::log::type::bootstrap, "Bootstrap is disabled, node will not be able to synchronize with the network");
		return;
	}

	workers.start ();

	if (config.enable_priorities)
	{
		priority_strat.start ();
	}

	if (config.enable_database_scan)
	{
		database_strat.start ();
	}

	if (config.enable_dependency_walker)
	{
		dependency_strat.start ();
	}

	if (config.enable_frontier_scan)
	{
		frontier_strat.start ();
	}

	if (config.enable_topo_scan)
	{
		topo_strat.start ();
	}

	maintenance_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_maintenance);
		run_maintenance ();
	});
}

void bootstrap_context::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	priority_strat.stop ();
	database_strat.stop ();
	dependency_strat.stop ();
	frontier_strat.stop ();
	topo_strat.stop ();
	nano::join_or_pass (maintenance_thread);

	workers.stop ();
}

void bootstrap_context::reset ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	stats.inc (nano::stat::type::bootstrap, nano::stat::detail::reset);
	logger.info (nano::log::type::bootstrap, "Resetting bootstrap state");

	accounts.reset ();
	database_scan.reset ();
	frontiers.reset ();
	peers.reset ();
	throttle.reset ();
	topo_strat.reset ();
}

bool bootstrap_context::send (std::shared_ptr<nano::transport::channel> const & channel, query_descriptor query, strategy source)
{
	return send (channel, std::move (query), source, generate_id ());
}

bool bootstrap_context::send (std::shared_ptr<nano::transport::channel> const & channel, query_descriptor query, strategy source, id_t id)
{
	async_tag tag{};
	tag.source = source;
	tag.query = std::move (query);
	tag.id = id;
	tag.node_id = channel->get_node_id ();

	// Derive the index keys from the query descriptor
	auto keys = index_keys (tag.query);
	tag.account = keys.account;
	tag.hash = keys.hash;

	// Build the outgoing message from the query descriptor
	auto message = build_message (tag.query, network_constants, tag.id);

	// Log the request
	struct log_visitor
	{
		nano::logger & logger;
		std::shared_ptr<nano::transport::channel> const & channel;
		std::string_view source;

		void operator() (blocks_query const & query) const
		{
			logger.debug (nano::log::type::bootstrap, "Requesting blocks for: {} starting from: {} count: {} from: {} ({})", query.account, query.start, query.count, channel, source);
		}
		void operator() (account_info_query const & query) const
		{
			logger.debug (nano::log::type::bootstrap, "Requesting account info for: {} from: {} ({})", query.target, channel, source);
		}
		void operator() (frontiers_query const & query) const
		{
			logger.debug (nano::log::type::bootstrap, "Requesting frontiers starting from: {} count: {} from: {} ({})", query.start, query.count, channel, source);
		}
		void operator() (topo_index_query const & query) const
		{
			logger.debug (nano::log::type::bootstrap, "Requesting topo index from height: {} hash: {} count: {} from: {} ({})", query.start.topo_height, query.start.hash, query.count, channel, source);
		}
		void operator() (blocks_random_query const & query) const
		{
			logger.debug (nano::log::type::bootstrap, "Requesting {} random blocks from: {} ({})", query.hashes.size (), channel, source);
		}
	};
	std::visit (log_visitor{ logger, channel, to_string (source) }, tag.query);

	// Note: a failed send deliberately does not release the reserved capacity; the elevated outstanding count acts as
	// an implicit penalty against an unresponsive peer until decay () heals it. Only a processed response releases.
	return transmit (channel, std::move (message), std::move (tag));
}

void bootstrap_context::conclude (async_tag const & tag, conclusion result)
{
	switch (tag.source)
	{
		case strategy::frontier:
		{
			switch (result)
			{
				case conclusion::timeout:
					frontier_strat.timeout (tag);
					break;
				case conclusion::failure:
					frontier_strat.failure (tag);
					break;
			}
		}
		break;
		case strategy::topology:
		{
			switch (result)
			{
				case conclusion::timeout:
					topo_strat.timeout (tag);
					break;
				case conclusion::failure:
					topo_strat.failure (tag);
					break;
			}
		}
		break;
		case strategy::invalid:
		case strategy::priority:
		case strategy::database:
		case strategy::dependency:
			break;
	}
}

bool bootstrap_context::conclude_tag (id_t id, conclusion result)
{
	debug_assert (!mutex.try_lock ());

	auto & tags_by_id = tags.get<tag_id> ();
	auto it = tags_by_id.find (id);
	if (it == tags_by_id.end ())
	{
		return false;
	}

	auto tag = *it;
	if (result == conclusion::timeout)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::timeout);
		stats.inc (nano::stat::type::bootstrap_timeout, to_stat_detail (tag.type ()));
	}
	conclude (tag, result);
	tags_by_id.erase (it);
	return true;
}

bool bootstrap_context::transmit (std::shared_ptr<nano::transport::channel> const & channel, nano::messages::asc_pull_req && message, async_tag tag)
{
	debug_assert (tag.type () != query_type::invalid);
	debug_assert (tag.source != strategy::invalid);

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
		// Give extra time for the request to be processed by the channel
		tag.cutoff = std::chrono::steady_clock::now () + config.request_timeout * 4;
		tags.get<tag_id> ().insert (tag);
	}

	bool sent = channel->send (
	message, nano::transport::traffic_type::bootstrap, [this, id = tag.id] (auto const & ec, auto size) {
		bool notify = false;
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			if (auto it = tags.get<tag_id> ().find (id); it != tags.get<tag_id> ().end ())
			{
				stats.inc (nano::stat::type::bootstrap_request_ec, nano::to_stat_detail (ec), nano::stat::dir::out);
				if (!ec)
				{
					stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request_success, nano::stat::dir::out);
					auto deadline = std::chrono::steady_clock::now () + config.request_timeout;
					tags.get<tag_id> ().modify (it, [deadline] (auto & tag) {
						// After the request has been sent, the peer has a limited time to respond
						tag.cutoff = deadline;
					});
					if (it->source == strategy::frontier)
					{
						frontier_strat.confirm (*it, deadline);
					}
				}
				else
				{
					stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request_failed, nano::stat::dir::out);
					auto tag = *it;
					conclude (tag, conclusion::failure);
					tags.get<tag_id> ().erase (it);
					notify = true;
				}
			}
		}
		if (notify)
		{
			condition.notify_all ();
		} });

	if (sent)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request);
		stats.inc (nano::stat::type::bootstrap_request, to_stat_detail (tag.type ()));
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request_failed);
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			if (auto it = tags.get<tag_id> ().find (tag.id); it != tags.get<tag_id> ().end ())
			{
				auto stored = *it;
				conclude (stored, conclusion::failure);
				tags.get<tag_id> ().erase (it);
			}
		}
		condition.notify_all ();
	}

	return sent;
}

void bootstrap_context::wait (std::function<bool ()> const & predicate) const
{
	std::unique_lock<nano::mutex> lock{ mutex };
	std::chrono::milliseconds interval = 5ms;
	while (!stopped && !predicate ())
	{
		condition.wait_for (lock, interval);
		interval = std::min (interval * 2, config.throttle_wait);
	}
}

void bootstrap_context::wait_block_processor (nano::bootstrap::strategy source) const
{
	auto const & channel = submission_channel (source);
	wait ([&] () {
		// Gate on this source's own fair-queue bucket, not the aggregate bootstrap backlog,
		// so sources don't block each other on a shared gauge.
		bool should_pass = block_processor.size (nano::block_source::bootstrap, channel) < config.block_processor_threshold;
		if (!should_pass)
		{
			stats.inc (nano::stat::type::bootstrap_wait_block_processor, to_stat_detail (source));
		}
		return should_pass;
	});
}

std::shared_ptr<nano::transport::channel> const & bootstrap_context::submission_channel (nano::bootstrap::strategy source) const
{
	switch (source)
	{
		case strategy::priority:
			return priority_channel;
		case strategy::database:
			return database_channel;
		case strategy::topology:
			return topology_channel;
		case strategy::invalid:
		case strategy::dependency:
		case strategy::frontier:
			break; // These strategies do not submit blocks to the processor
	}
	debug_assert (false);
	return generic_channel; // Generic origin, doesn't alias either partition
}

std::shared_ptr<nano::transport::channel> bootstrap_context::wait_channel (nano::bootstrap::strategy strat, peer_requirements const & required)
{
	auto & strategy_limiter = [this, strat] () -> nano::rate_limiter & {
		switch (strat)
		{
			case strategy::priority:
				return priority_limiter;
			case strategy::database:
				return database_limiter;
			case strategy::dependency:
				return dependency_limiter;
			case strategy::frontier:
				return frontier_limiter;
			case strategy::topology:
				return topo_limiter;
			case strategy::invalid:
				break;
		}
		release_assert (false);
	}();

	// Limit the number of in-flight requests
	wait ([this] () {
		return tags.size () < config.max_requests;
	});

	// Wait until more requests can be sent (per-strategy rate limit)
	wait ([&strategy_limiter] () {
		return strategy_limiter.try_consume (1);
	});

	// Wait until a channel is available
	return wait_result ([this, strat, required] () {
		auto result = peers.acquire (required);
		if (!result.channel)
		{
			stats.inc (nano::stat::type::bootstrap_wait_channel, to_stat_detail (strat));
		}
		return result.channel;
	});
}

bootstrap_context::fanout_result bootstrap_context::wait_channels (nano::bootstrap::strategy strat, peer_requirements const & required, std::span<nano::account const> exclude, unsigned max)
{
	auto & strategy_limiter = [this, strat] () -> nano::rate_limiter & {
		switch (strat)
		{
			case strategy::priority:
				return priority_limiter;
			case strategy::database:
				return database_limiter;
			case strategy::dependency:
				return dependency_limiter;
			case strategy::frontier:
				return frontier_limiter;
			case strategy::topology:
				return topo_limiter;
			case strategy::invalid:
				break;
		}
		release_assert (false);
	}();

	fanout_result result;

	// Working exclusion: the caller's set plus every peer we pick, so each lease is a distinct peer
	std::vector<nano::account> used (exclude.begin (), exclude.end ());

	for (unsigned i = 0; i < max; ++i)
	{
		if (i == 0)
		{
			// Block for the first lease so every round (fresh or top-up) makes progress: wait out transient
			// busyness and a momentarily empty peer set, but bail the instant the distinct-peer pool is
			// exhausted so the caller can advance with however many it has.
			wait ([this] () {
				return tags.size () < config.max_requests;
			});
			// Wait until more requests can be sent (per-strategy rate limit)
			wait ([&strategy_limiter] () {
				return strategy_limiter.try_consume (1);
			});
			// Wait until a non-excluded channel is available, or the matching pool is exhausted
			peer_pool::acquire_result acq;
			wait ([this, &acq, strat, &required, &used] () {
				acq = peers.acquire (required, used);
				if (!acq.channel && acq.status != peer_acquire_status::exhausted)
				{
					stats.inc (nano::stat::type::bootstrap_wait_channel, to_stat_detail (strat));
				}
				return acq.channel != nullptr || acq.status == peer_acquire_status::exhausted;
			});
			if (!acq.channel)
			{
				result.exhausted = (acq.status == peer_acquire_status::exhausted);
				break; // exhausted (advance with fewer) or stopped
			}
			result.leases.push_back ({ acq.channel, acq.node_id });
			used.push_back (acq.node_id);
		}
		else
		{
			// Best-effort: take an immediately-available distinct peer, otherwise stop the round
			nano::lock_guard<nano::mutex> lock{ mutex };
			if (stopped || tags.size () >= config.max_requests)
			{
				break;
			}
			auto acq = peers.acquire (required, used);
			if (!acq.channel)
			{
				result.exhausted = (acq.status == peer_acquire_status::exhausted);
				break;
			}
			if (!strategy_limiter.try_consume (1))
			{
				peers.release (acq.channel); // rate limited: give the reservation back
				break;
			}
			result.leases.push_back ({ acq.channel, acq.node_id });
			used.push_back (acq.node_id);
		}
	}

	return result;
}

size_t bootstrap_context::count_tags (nano::account const & account, strategy source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_account> ().equal_range (account);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

size_t bootstrap_context::count_tags (nano::block_hash const & hash, strategy source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_hash> ().equal_range (hash);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

/**
 * Inspects a block that has been processed by the block processor
 * - Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
 * - Marks an account as forwarded if it has been recently referenced by a block that has been inserted
 */
void bootstrap_context::inspect (secure::transaction const & tx, nano::block_status const & result, nano::block_context const & context)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (context.block != nullptr);

	auto const & block = *context.block;
	auto const & source = context.source;
	auto const & hash = block.hash ();

	auto const tag_source = [&context] {
		if (auto const * tag_source = std::any_cast<strategy> (&context.tag))
		{
			return *tag_source;
		}
		return strategy::invalid;
	}();

	switch (result)
	{
		case nano::block_status::progress:
		{
			// Progress blocks from live traffic don't need further bootstrapping
			if (source != nano::block_source::live)
			{
				const auto account = block.account ();

				// If we've inserted any block in to an account, unmark it as blocked
				accounts.unblock (account);
				accounts.priority_up (account);

				if (block.is_send ())
				{
					auto destination = block.destination ();
					accounts.unblock (destination, hash); // Unblocking automatically inserts account into priority set
					accounts.priority_set (destination);
				}
			}
		}
		break;
		case nano::block_status::gap_source:
		{
			// Prevent malicious live traffic from filling up the blocked set
			if (source == nano::block_source::bootstrap)
			{
				const auto account = block.previous ().is_zero () ? block.account_field ().value () : ledger.any.block_account (tx, block.previous ()).value_or (0);
				const auto source_hash = block.source_field ().value_or (block.link_field ().value_or (0).as_block_hash ());

				if (!account.is_zero () && !source_hash.is_zero ())
				{
					// Mark account as blocked because it is missing the source block
					accounts.block (account, source_hash);
				}
			}
		}
		break;
		case nano::block_status::gap_previous:
		{
			// Prevent live traffic from evicting accounts from the priority list
			if (source == nano::block_source::live && !accounts.priority_half_full () && !accounts.blocked_half_full ())
			{
				if (block.type () == block_type::state)
				{
					const auto account = block.account_field ().value ();
					accounts.priority_set (account);
				}
			}
		}
		break;
		default: // No need to handle other cases
			// TODO: If we receive blocks that are invalid (bad signature, fork, etc.), we should penalize the peer that sent them
			break;
	}

	if (source == nano::block_source::bootstrap)
	{
		stats.inc (nano::stat::type::bootstrap_inspect, nano::to_stat_detail (result));
		stats.inc (nano::stat::type::bootstrap_inspect_source, to_stat_detail (tag_source));
		stats.inc (to_inspect_stat_type (tag_source), nano::to_stat_detail (result));
	}

	// Topo strategy tracks its own submitted blocks that came back as gaps
	topo_strat.inspect (result, block, tag_source);
}

void bootstrap_context::rollback (nano::block const & block)
{
	debug_assert (!mutex.try_lock ());

	auto const account = block.account ();

	// The dependency this account was blocked on is no longer valid, so reopen it for re-evaluation
	accounts.unblock (account);

	// Drop any gaps the topo strategy was tracking for this account; its ancestors changed under us
	topo_strat.rollback (account);
}

void bootstrap_context::maintenance (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	// Snapshot peers without the bootstrap mutex held, to avoid nesting the network mutex under it
	lock.unlock ();
	// Only request ledger data from peers that both meet the bootstrap protocol version and serve a ledger
	auto const min_version = network_constants.bootstrap_protocol_version_min;
	auto channels = network.list (/* all */ 0, [min_version] (auto const & channel) {
		return channel->get_network_version () >= min_version && channel->serves_ledger ();
	});
	lock.lock ();

	peers.update (channels);
	peers.decay ();

	throttle.resize (compute_throttle_size ());

	accounts.decay_blocking ();

	topo_strat.maintenance ();

	auto const now = std::chrono::steady_clock::now ();
	auto should_timeout = [&] (async_tag const & tag) {
		return tag.cutoff < now;
	};

	// Erase timed out requests
	auto & tags_by_order = tags.get<tag_sequenced> ();
	for (auto it = tags_by_order.begin (); it != tags_by_order.end ();)
	{
		if (should_timeout (*it))
		{
			auto tag = *it;
			stats.inc (nano::stat::type::bootstrap, nano::stat::detail::timeout);
			stats.inc (nano::stat::type::bootstrap_timeout, to_stat_detail (tag.type ()));
			conclude (tag, conclusion::timeout);
			it = tags_by_order.erase (it);
		}
		else
		{
			++it;
		}
	}

	condition.notify_all ();
}

void bootstrap_context::run_maintenance ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_maintenance);
		maintenance (lock);
		condition.wait_for (lock, nano::is_dev_run () ? 500ms : 5s, [this] () { return stopped; });
	}
}

void bootstrap_context::process (nano::messages::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// Only process messages that have a known tag
	auto it = tags.get<tag_id> ().find (message.id);
	if (it == tags.get<tag_id> ().end ())
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::missing_tag);
		return;
	}

	stats.inc (nano::stat::type::bootstrap, nano::stat::detail::reply);

	auto tag = *it;
	tags.get<tag_id> ().erase (it); // Iterator is invalid after this point

	// Verifies that response type corresponds to our query
	struct payload_verifier
	{
		query_type type;

		bool operator() (const nano::messages::asc_pull_ack::blocks_payload & response) const
		{
			// A blocks_random response reuses the blocks_payload format
			return type == query_type::blocks_by_hash || type == query_type::blocks_by_account || type == query_type::blocks_random;
		}
		bool operator() (const nano::messages::asc_pull_ack::account_info_payload & response) const
		{
			return type == query_type::account_info_by_hash;
		}
		bool operator() (const nano::messages::asc_pull_ack::frontiers_payload & response) const
		{
			return type == query_type::frontiers;
		}
		bool operator() (const nano::messages::asc_pull_ack::topo_index_payload & response) const
		{
			return type == query_type::topo_index;
		}
		bool operator() (const nano::messages::empty_payload & response) const
		{
			return false; // Should not happen
		}
	};

	bool valid = std::visit (payload_verifier{ tag.type () }, message.payload);
	if (!valid)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::invalid_response_type);
		conclude (tag, conclusion::failure);
		lock.unlock ();
		condition.notify_all ();
		return;
	}

	// Track bootstrap request response time
	stats.inc (nano::stat::type::bootstrap_reply, to_stat_detail (tag.type ()));
	stats.sample (nano::stat::sample::bootstrap_tag_duration, nano::log::milliseconds_delta (tag.timestamp), { 0, config.request_timeout.count () });

	// Process the response payload while holding the lock to ensure atomic tag erasure + state updates
	bool ok = std::visit ([this, &tag] (auto && request) { return process (request, tag); }, message.payload);
	if (ok)
	{
		peers.release (channel);
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::invalid_response);
	}

	lock.unlock ();

	condition.notify_all ();
}

bool bootstrap_context::process (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag)
{
	debug_assert (!mutex.try_lock ());

	// A blocks_random response reuses the blocks_payload format but belongs to the topology strategy
	if (tag.type () == query_type::blocks_random)
	{
		return topo_strat.process (response, tag);
	}

	debug_assert (tag.type () == query_type::blocks_by_hash || tag.type () == query_type::blocks_by_account);

	release_assert (std::holds_alternative<blocks_query> (tag.query));
	auto const & query = std::get<blocks_query> (tag.query);

	stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::blocks);

	auto result = verify (response, query);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (nano::stat::type::bootstrap_verify_blocks, nano::stat::detail::ok);
			stats.add (nano::stat::type::bootstrap, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

			workers.post ([this, blocks = response.blocks, tag] () mutable {
				submit_blocks (std::move (blocks), tag);
			});
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_verify_blocks, nano::stat::detail::nothing_new);
			{
				accounts.priority_down (tag.account);
				accounts.timestamp_reset (tag.account);

				if (tag.source == strategy::database)
				{
					throttle.add (false);
				}
			}
			condition.notify_all ();
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (nano::stat::type::bootstrap_verify_blocks, nano::stat::detail::invalid);
		}
		break;
	}

	return result != verify_result::invalid;
}

void bootstrap_context::submit_blocks (std::deque<std::shared_ptr<nano::block>> blocks, async_tag const & tag)
{
	release_assert (std::holds_alternative<blocks_query> (tag.query));
	auto source = tag.source;
	auto const & query = std::get<blocks_query> (tag.query);

	// Avoid re-processing the block we already have (the echoed cursor)
	release_assert (!blocks.empty ());
	if (blocks.front ()->hash () == query.start.as_block_hash ())
	{
		blocks.pop_front ();
	}

	// Drop the leading run of blocks already in the ledger, otherwise they take a slot in the
	// block processor's bounded queue only to come back as `old`. Common when priority and topology run together.
	// The response is a contiguous chain, so present blocks form a prefix: stop at the first missing block, and the rest have their `previous` in the ledger.
	size_t filtered = 0;
	{
		auto transaction = ledger.tx_begin_read ();
		while (!blocks.empty () && ledger.any.block_exists_or_pruned (transaction, blocks.front ()->hash ()))
		{
			blocks.pop_front ();
			++filtered;
		}
	}
	stats.add (nano::stat::type::bootstrap, nano::stat::detail::filtered_blocks, filtered);

	if (blocks.empty ())
	{
		// Whole response already in the ledger. Mirror an all-`old` submission: release the account
		// cooldown for re-sampling without lowering priority, and count the database pull as unproductive so the throttle can back off.
		{
			nano::lock_guard<nano::mutex> guard{ mutex };
			accounts.timestamp_reset (tag.account);
			if (source == strategy::database)
			{
				throttle.add (false);
			}
		}
		condition.notify_all ();
		return;
	}

	auto result = block_processor.add_many (
	blocks, nano::block_source::bootstrap, submission_channel (source), [this, account = tag.account] (auto result) {
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::submission_complete);
		{
			// It's the last block submitted for this account chain, reset timestamp to allow more requests
			nano::lock_guard<nano::mutex> guard{ mutex };
			accounts.timestamp_reset (account);
		}
		condition.notify_all ();
	},
	source);

	// Drops should not happen, submissions are gated by wait_block_processor()
	// A dropped tail loses the completion callback, leaving the account stuck on cooldown
	if (result.dropped > 0)
	{
		stats.add (nano::stat::type::bootstrap, nano::stat::detail::overfill, result.dropped);
		debug_assert (false, "bootstrap block processor dropped submitted blocks");
	}

	if (source == strategy::database)
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		throttle.add (true);
	}
}

bool bootstrap_context::process (nano::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag)
{
	return dependency_strat.process (response, tag);
}

bool bootstrap_context::process (nano::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag)
{
	return frontier_strat.process (response, tag);
}

bool bootstrap_context::process (nano::messages::asc_pull_ack::topo_index_payload const & response, async_tag const & tag)
{
	return topo_strat.process (response, tag);
}

bool bootstrap_context::process (nano::messages::empty_payload const & response, async_tag const & tag)
{
	stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::empty);
	debug_assert (false, "empty payload"); // Should not happen
	return false; // Invalid
}

std::size_t bootstrap_context::compute_throttle_size () const
{
	auto ledger_size = ledger.account_count ();
	size_t target = ledger_size > 0 ? config.throttle_coefficient * static_cast<size_t> (std::log (ledger_size)) : 0;
	size_t min_size = 16;
	return std::max (target, min_size);
}

nano::container_info bootstrap_context::container_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto collect_limiters = [this] () {
		nano::container_info info;
		info.put ("priority", priority_limiter.available ());
		info.put ("database", database_limiter.available ());
		info.put ("dependency", dependency_limiter.available ());
		info.put ("frontier", frontier_limiter.available ());
		info.put ("topo", topo_limiter.available ());
		return info;
	};

	nano::container_info info;
	info.put ("tags", tags);
	info.put ("throttle", throttle.size ());
	info.put ("throttle_successes", throttle.successes ());
	info.add ("accounts", accounts.container_info ());
	info.add ("database_scan", database_scan.container_info ());
	info.add ("frontiers", frontiers.container_info ());
	info.add ("topo", topo_strat.container_info ());
	info.add ("workers", workers.container_info ());
	info.add ("peers", peers.container_info ());
	info.add ("limiters", collect_limiters ());
	return info;
}

/*
 *
 */

query_type async_tag::type () const
{
	return to_query_type (query);
}
}
