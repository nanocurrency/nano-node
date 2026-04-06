#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
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
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>

using namespace std::chrono_literals;

namespace nano::bootstrap
{
bootstrap_context::bootstrap_context (nano::node_config const & node_config_a, nano::ledger & ledger_a,
nano::ledger_notifications & ledger_notifications_a, nano::block_processor & block_processor_a, nano::network & network_a, nano::stats & stat_a, nano::logger & logger_a) :
	config{ node_config_a.bootstrap },
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
	accounts{ config.account_sets, stats },
	database_scan{ ledger },
	frontiers{ config.frontier_scan, stats },
	throttle{ compute_throttle_size () },
	scoring{ config, node_config_a.network_params.network },
	limiter{ config.rate_limit },
	database_limiter{ config.database_rate_limit },
	frontiers_limiter{ config.frontier_rate_limit },
	workers{ 1, nano::thread_role::name::bootstrap_worker }
{
	// Inspect all processed blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };

			auto transaction = ledger.tx_begin_read ();
			for (auto const & [result, block_context] : batch)
			{
				debug_assert (block_context.block != nullptr);
				inspect (transaction, result, *block_context.block, block_context.source);
			}
		}
		condition.notify_all ();
	});

	// Unblock rolled back accounts as the dependency is no longer valid
	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		nano::lock_guard<nano::mutex> lock{ mutex };
		for (auto const & block : blocks)
		{
			debug_assert (block != nullptr);
			accounts.unblock (block->account ());
		}
	});

	accounts.priority_set (node_config_a.network_params.ledger.genesis->account_field ().value ());
}

bootstrap_context::~bootstrap_context ()
{
	// All threads must be stopped before destruction
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!workers.alive ());
}

void bootstrap_context::start ()
{
	debug_assert (!cleanup_thread.joinable ());

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

	cleanup_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_cleanup);
		run_cleanup ();
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
	nano::join_or_pass (cleanup_thread);

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
	scoring.reset ();
	throttle.reset ();
}

bool bootstrap_context::send (std::shared_ptr<nano::transport::channel> const & channel, nano::messages::asc_pull_req && message, async_tag tag)
{
	debug_assert (tag.type != query_type::invalid);
	debug_assert (tag.source != query_source::invalid);

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
		// Give extra time for the request to be processed by the channel
		tag.cutoff = std::chrono::steady_clock::now () + config.request_timeout * 4;
		tags.get<tag_id> ().insert (tag);
	}

	bool sent = channel->send (
	message, nano::transport::traffic_type::bootstrap_requests, [this, id = tag.id] (auto const & ec, auto size) {
		nano::lock_guard<nano::mutex> lock{ mutex };
		if (auto it = tags.get<tag_id> ().find (id); it != tags.get<tag_id> ().end ())
		{
			stats.inc (nano::stat::type::bootstrap_request_ec, nano::to_stat_detail (ec), nano::stat::dir::out);
			if (!ec)
			{
				stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request_success, nano::stat::dir::out);
				tags.get<tag_id> ().modify (it, [&] (auto & tag) {
					// After the request has been sent, the peer has a limited time to respond
					tag.cutoff = std::chrono::steady_clock::now () + config.request_timeout;
				});
			}
			else
			{
				stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request_failed, nano::stat::dir::out);
				tags.get<tag_id> ().erase (it);
			}
		} });

	if (sent)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request);
		stats.inc (nano::stat::type::bootstrap_request, to_stat_detail (tag.type));
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::request_failed);
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

void bootstrap_context::wait_block_processor () const
{
	wait ([this] () {
		return block_processor.size (nano::block_source::bootstrap) < config.block_processor_threshold;
	});
}

std::shared_ptr<nano::transport::channel> bootstrap_context::wait_channel ()
{
	// Limit the number of in-flight requests
	wait ([this] () {
		return tags.size () < config.max_requests;
	});

	// Wait until more requests can be sent
	wait ([this] () {
		return limiter.should_pass (1);
	});

	// Wait until a channel is available
	std::shared_ptr<nano::transport::channel> channel;
	wait ([this, &channel] () {
		channel = scoring.channel ();
		return channel != nullptr; // Wait until a channel is available
	});
	return channel;
}

size_t bootstrap_context::count_tags (nano::account const & account, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_account> ().equal_range (account);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

size_t bootstrap_context::count_tags (nano::block_hash const & hash, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_hash> ().equal_range (hash);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

bool bootstrap_context::request (nano::account account, size_t count, std::shared_ptr<nano::transport::channel> const & channel, query_source source)
{
	debug_assert (count > 0);
	debug_assert (count <= nano::bootstrap_server::max_blocks);

	// Limit the max number of blocks to pull
	count = std::min (count, config.max_pull_count);

	async_tag tag{};
	tag.source = source;
	tag.account = account;

	blocks_tag_payload payload{};
	payload.count = count;

	{
		auto transaction = ledger.store.tx_begin_read ();

		// Check if the account picked has blocks, if it does, start the pull from the highest block
		if (auto info = ledger.store.account.get (transaction, account))
		{
			// Probabilistically choose between requesting blocks from account frontier or confirmed frontier
			// Optimistic requests start from the (possibly unconfirmed) account frontier and are vulnerable to bootstrap poisoning
			// Safe requests start from the confirmed frontier and given enough time will eventually resolve forks
			bool const optimistic_request = rng.random (100) < config.optimistic_request_percentage;

			if (optimistic_request) // Optimistic request case
			{
				stats.inc (nano::stat::type::bootstrap_request_blocks, nano::stat::detail::optimistic);

				tag.type = query_type::blocks_by_hash;
				payload.start = info->head;
				tag.hash = info->head;

				logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} starting from account frontier: {} (optimistic: {}) from: {}",
				account,
				payload.start,
				optimistic_request,
				channel);
			}
			else // Pessimistic (safe) request case
			{
				stats.inc (nano::stat::type::bootstrap_request_blocks, nano::stat::detail::safe);

				if (auto conf_info = ledger.store.confirmation_height.get (transaction, account))
				{
					tag.type = query_type::blocks_by_hash;
					payload.start = conf_info->frontier;
					tag.hash = conf_info->frontier;

					logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} starting from confirmation frontier: {} (optimistic: {}) from: {}",
					account,
					payload.start,
					optimistic_request,
					channel);
				}
				else
				{
					tag.type = query_type::blocks_by_account;
					payload.start = account;

					logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} starting from account root (optimistic: {}) from: {}",
					account,
					optimistic_request,
					channel);
				}
			}
		}
		else
		{
			stats.inc (nano::stat::type::bootstrap_request_blocks, nano::stat::detail::base);

			tag.type = query_type::blocks_by_account;
			payload.start = account;

			logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} from: {}", account, channel);
		}
	}

	tag.payload = payload;

	// Build the message
	nano::messages::asc_pull_req message{ network_constants };
	message.id = tag.id;
	message.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload msg_pld;
	msg_pld.start = payload.start;
	msg_pld.count = payload.count;
	msg_pld.start_type = tag.type == query_type::blocks_by_hash ? nano::messages::asc_pull_req::hash_type::block : nano::messages::asc_pull_req::hash_type::account;
	message.payload = msg_pld;
	message.update_header ();

	return send (channel, std::move (message), tag);
}

/**
 * Inspects a block that has been processed by the block processor
 * - Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
 * - Marks an account as forwarded if it has been recently referenced by a block that has been inserted
 */
void bootstrap_context::inspect (secure::transaction const & tx, nano::block_status const & result, nano::block const & block, nano::block_source source)
{
	debug_assert (!mutex.try_lock ());

	auto const hash = block.hash ();

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
		case nano::block_status::gap_epoch_open_pending:
		{
			// Epoch open blocks for accounts that don't have any pending blocks yet
			debug_assert (block.type () == block_type::state); // Only state blocks can have epoch open pending status
			const auto account = block.account_field ().value_or (0);
			accounts.priority_erase (account);
		}
		break;
		default: // No need to handle other cases
			// TODO: If we receive blocks that are invalid (bad signature, fork, etc.), we should penalize the peer that sent them
			break;
	}
}

void bootstrap_context::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	scoring.sync (network.list (/* all */ 0, network_constants.bootstrap_protocol_version_min));
	scoring.timeout ();

	throttle.resize (compute_throttle_size ());

	accounts.decay_blocking ();

	auto const now = std::chrono::steady_clock::now ();
	auto should_timeout = [&] (async_tag const & tag) {
		return tag.cutoff < now;
	};

	// Erase timed out requests
	auto & tags_by_order = tags.get<tag_sequenced> ();
	while (!tags_by_order.empty () && should_timeout (tags_by_order.front ()))
	{
		auto tag = tags_by_order.front ();
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::timeout);
		stats.inc (nano::stat::type::bootstrap_timeout, to_stat_detail (tag.type));
		tags_by_order.pop_front ();
	}
}

void bootstrap_context::run_cleanup ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_cleanup);
		cleanup ();
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
			return type == query_type::blocks_by_hash || type == query_type::blocks_by_account;
		}
		bool operator() (const nano::messages::asc_pull_ack::account_info_payload & response) const
		{
			return type == query_type::account_info_by_hash;
		}
		bool operator() (const nano::messages::asc_pull_ack::frontiers_payload & response) const
		{
			return type == query_type::frontiers;
		}
		bool operator() (const nano::messages::empty_payload & response) const
		{
			return false; // Should not happen
		}
	};

	bool valid = std::visit (payload_verifier{ tag.type }, message.payload);
	if (!valid)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::invalid_response_type);
		return;
	}

	// Track bootstrap request response time
	stats.inc (nano::stat::type::bootstrap_reply, to_stat_detail (tag.type));
	stats.sample (nano::stat::sample::bootstrap_tag_duration, nano::log::milliseconds_delta (tag.timestamp), { 0, config.request_timeout.count () });

	// Process the response payload while holding the lock to ensure atomic tag erasure + state updates
	bool ok = std::visit ([this, &tag] (auto && request) { return process (request, tag); }, message.payload);
	if (ok)
	{
		scoring.received_message (channel);
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
	debug_assert (tag.type == query_type::blocks_by_hash || tag.type == query_type::blocks_by_account);

	release_assert (std::holds_alternative<blocks_tag_payload> (tag.payload));
	auto const & payload = std::get<blocks_tag_payload> (tag.payload);

	stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::blocks);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (nano::stat::type::bootstrap_verify_blocks, nano::stat::detail::ok);
			stats.add (nano::stat::type::bootstrap, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

			auto blocks = response.blocks;

			// Avoid re-processing the block we already have
			release_assert (blocks.size () >= 1);
			if (blocks.front ()->hash () == payload.start.as_block_hash ())
			{
				blocks.pop_front ();
			}

			block_processor.add_many (blocks, nano::block_source::bootstrap, nullptr, [this, account = tag.account] (auto result) {
				// It's the last block submitted for this account chain, reset timestamp to allow more requests
				stats.inc (nano::stat::type::bootstrap, nano::stat::detail::timestamp_reset);
				{
					nano::lock_guard<nano::mutex> guard{ mutex };
					accounts.timestamp_reset (account);
				}
				condition.notify_all ();
			});

			if (tag.source == query_source::database)
			{
				throttle.add (true);
			}
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_verify_blocks, nano::stat::detail::nothing_new);
			{
				accounts.priority_down (tag.account);
				accounts.timestamp_reset (tag.account);

				if (tag.source == query_source::database)
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

verify_result bootstrap_context::verify (nano::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag) const
{
	release_assert (std::holds_alternative<blocks_tag_payload> (tag.payload));
	auto const & payload = std::get<blocks_tag_payload> (tag.payload);
	auto const & blocks = response.blocks;

	if (blocks.empty ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () == 1 && blocks.front ()->hash () == payload.start.as_block_hash ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () > payload.count)
	{
		return verify_result::invalid;
	}

	auto const & first = blocks.front ();
	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		{
			if (first->hash () != payload.start.as_block_hash ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		case query_type::blocks_by_account:
		{
			// Open & state blocks always contain account field
			if (first->account_field ().value_or (0) != payload.start.as_account ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		default:
			return verify_result::invalid;
	}

	// Verify blocks make a valid chain
	nano::block_hash previous_hash = blocks.front ()->hash ();
	for (int n = 1; n < blocks.size (); ++n)
	{
		auto & block = blocks[n];
		if (block->previous () != previous_hash)
		{
			// TODO: Stat & log
			return verify_result::invalid; // Blocks do not make a chain
		}
		previous_hash = block->hash ();
	}

	return verify_result::ok;
}

bool bootstrap_context::process (nano::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag)
{
	return dependency_strat.process (response, tag);
}

bool bootstrap_context::process (nano::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag)
{
	return frontier_strat.process (response, tag);
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
		info.put ("total", limiter.size ());
		info.put ("database", database_limiter.size ());
		info.put ("frontiers", frontiers_limiter.size ());
		return info;
	};

	nano::container_info info;
	info.put ("tags", tags);
	info.put ("throttle", throttle.size ());
	info.put ("throttle_successes", throttle.successes ());
	info.add ("accounts", accounts.container_info ());
	info.add ("database_scan", database_scan.container_info ());
	info.add ("frontiers", frontiers.container_info ());
	info.add ("workers", workers.container_info ());
	info.add ("peers", scoring.container_info ());
	info.add ("limiters", collect_limiters ());
	return info;
}
}
