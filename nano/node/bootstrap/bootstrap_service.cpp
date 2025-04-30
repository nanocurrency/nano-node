#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/bootstrap/crawlers.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>

using namespace std::chrono_literals;

nano::bootstrap_service::bootstrap_service (nano::node_config const & node_config_a, nano::ledger & ledger_a, nano::ledger_notifications & ledger_notifications_a,
nano::block_processor & block_processor_a, nano::network & network_a, nano::stats & stat_a, nano::logger & logger_a) :
	config{ node_config_a.bootstrap },
	network_constants{ node_config_a.network_params.network },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	block_processor{ block_processor_a },
	network{ network_a },
	stats{ stat_a },
	logger{ logger_a },
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
			for (auto const & [result, context] : batch)
			{
				debug_assert (context.block != nullptr);
				inspect (transaction, result, *context.block, context.source);
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

nano::bootstrap_service::~bootstrap_service ()
{
	// All threads must be stopped before destruction
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!database_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!frontiers_thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!workers.alive ());
}

void nano::bootstrap_service::start ()
{
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!database_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!frontiers_thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());

	if (!config.enable)
	{
		logger.warn (nano::log::type::bootstrap, "Bootstrap is disabled, node will not be able to synchronize with the network");
		return;
	}

	workers.start ();

	if (config.enable_priorities)
	{
		priorities_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::bootstrap);
			run_priorities ();
		});
	}

	if (config.enable_database_scan)
	{
		database_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::bootstrap_database_scan);
			run_database ();
		});
	}

	if (config.enable_dependency_walker)
	{
		dependencies_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::bootstrap_dependency_walker);
			run_dependencies ();
		});
	}

	if (config.enable_frontier_scan)
	{
		frontiers_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::bootstrap_frontier_scan);
			run_frontiers ();
		});
	}

	cleanup_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::bootstrap_cleanup);
		run_timeouts ();
	});
}

void nano::bootstrap_service::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	nano::join_or_pass (priorities_thread);
	nano::join_or_pass (database_thread);
	nano::join_or_pass (dependencies_thread);
	nano::join_or_pass (frontiers_thread);
	nano::join_or_pass (cleanup_thread);

	workers.stop ();
}

void nano::bootstrap_service::reset ()
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

void nano::bootstrap_service::prioritize (nano::account const & account)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	accounts.priority_set (account);
}

bool nano::bootstrap_service::send (std::shared_ptr<nano::transport::channel> const & channel, async_tag tag)
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

	nano::asc_pull_req request{ network_constants };
	request.id = tag.id;

	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		case query_type::blocks_by_account:
		{
			request.type = nano::asc_pull_type::blocks;

			nano::asc_pull_req::blocks_payload pld;
			pld.start = tag.start;
			pld.count = tag.count;
			pld.start_type = tag.type == query_type::blocks_by_hash ? nano::asc_pull_req::hash_type::block : nano::asc_pull_req::hash_type::account;
			request.payload = pld;
		}
		break;
		case query_type::account_info_by_hash:
		{
			request.type = nano::asc_pull_type::account_info;

			nano::asc_pull_req::account_info_payload pld;
			pld.target_type = nano::asc_pull_req::hash_type::block; // Query account info by block hash
			pld.target = tag.start;
			request.payload = pld;
		}
		break;
		case query_type::frontiers:
		{
			request.type = nano::asc_pull_type::frontiers;

			nano::asc_pull_req::frontiers_payload pld;
			pld.start = tag.start.as_account ();
			pld.count = nano::asc_pull_ack::frontiers_payload::max_frontiers;
			request.payload = pld;
		}
		break;
		default:
			debug_assert (false);
	}

	request.update_header ();

	bool sent = channel->send (
	request, nano::transport::traffic_type::bootstrap_requests, [this, id = tag.id] (auto const & ec, auto size) {
		nano::lock_guard<nano::mutex> lock{ mutex };
		if (auto it = tags.get<tag_id> ().find (id); it != tags.get<tag_id> ().end ())
		{
			stats.inc (nano::stat::type::bootstrap_request_ec, to_stat_detail (ec), nano::stat::dir::out);
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

std::size_t nano::bootstrap_service::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.priority_size ();
}

std::size_t nano::bootstrap_service::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

std::size_t nano::bootstrap_service::score_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return scoring.size ();
}

bool nano::bootstrap_service::prioritized (nano::account const & account) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.prioritized (account);
}

bool nano::bootstrap_service::blocked (nano::account const & account) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked (account);
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap_service::inspect (secure::transaction const & tx, nano::block_status const & result, nano::block const & block, nano::block_source source)
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

void nano::bootstrap_service::wait (std::function<bool ()> const & predicate) const
{
	std::unique_lock<nano::mutex> lock{ mutex };
	std::chrono::milliseconds interval = 5ms;
	while (!stopped && !predicate ())
	{
		condition.wait_for (lock, interval);
		interval = std::min (interval * 2, config.throttle_wait);
	}
}

void nano::bootstrap_service::wait_block_processor () const
{
	wait ([this] () {
		return block_processor.size (nano::block_source::bootstrap) < config.block_processor_threshold;
	});
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_service::wait_channel ()
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

size_t nano::bootstrap_service::count_tags (nano::account const & account, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_account> ().equal_range (account);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

size_t nano::bootstrap_service::count_tags (nano::block_hash const & hash, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_hash> ().equal_range (hash);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

nano::bootstrap::account_sets::priority_result nano::bootstrap_service::next_priority ()
{
	debug_assert (!mutex.try_lock ());

	auto next = accounts.next_priority ([this] (nano::account const & account) {
		return count_tags (account, query_source::priority) < 4;
	});
	if (next.account.is_zero ())
	{
		return {};
	}
	stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_priority);
	return next;
}

nano::bootstrap::account_sets::priority_result nano::bootstrap_service::wait_priority ()
{
	nano::bootstrap::account_sets::priority_result result{};
	wait ([this, &result] () {
		debug_assert (!mutex.try_lock ());
		result = next_priority ();
		if (!result.account.is_zero ())
		{
			return true;
		}
		return false;
	});
	return result;
}

nano::account nano::bootstrap_service::next_database (bool should_throttle)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (config.database_warmup_ratio > 0);

	// Throttling increases the weight of database requests
	if (!database_limiter.should_pass (should_throttle ? config.database_warmup_ratio : 1))
	{
		return { 0 };
	}
	auto account = database_scan.next ([this] (nano::account const & account) {
		return count_tags (account, query_source::database) == 0;
	});
	if (account.is_zero ())
	{
		return { 0 };
	}
	stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_database);
	return account;
}

nano::account nano::bootstrap_service::wait_database (bool should_throttle)
{
	nano::account result{ 0 };
	wait ([this, &result, should_throttle] () {
		debug_assert (!mutex.try_lock ());
		result = next_database (should_throttle);
		if (!result.is_zero ())
		{
			return true;
		}
		return false;
	});
	return result;
}

nano::block_hash nano::bootstrap_service::next_blocking ()
{
	debug_assert (!mutex.try_lock ());

	auto blocking = accounts.next_blocking ([this] (nano::block_hash const & hash) {
		return count_tags (hash, query_source::dependencies) == 0;
	});
	if (blocking.is_zero ())
	{
		return { 0 };
	}
	stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_blocking);
	return blocking;
}

nano::block_hash nano::bootstrap_service::wait_blocking ()
{
	nano::block_hash result{ 0 };
	wait ([this, &result] () {
		debug_assert (!mutex.try_lock ());
		result = next_blocking ();
		if (!result.is_zero ())
		{
			return true;
		}
		return false;
	});
	return result;
}

nano::account nano::bootstrap_service::wait_frontier ()
{
	nano::account result{ 0 };
	wait ([this, &result] () {
		debug_assert (!mutex.try_lock ());
		result = frontiers.next ();
		if (!result.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_next, nano::stat::detail::next_frontier);
			return true;
		}
		return false;
	});
	return result;
}

bool nano::bootstrap_service::request (nano::account account, size_t count, std::shared_ptr<nano::transport::channel> const & channel, query_source source)
{
	debug_assert (count > 0);
	debug_assert (count <= nano::bootstrap_server::max_blocks);

	// Limit the max number of blocks to pull
	count = std::min (count, config.max_pull_count);

	async_tag tag{};
	tag.source = source;
	tag.account = account;
	tag.count = count;

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
				tag.start = info->head;
				tag.hash = info->head;

				logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} starting from account frontier: {} (optimistic: {}) from: {}",
				account,
				tag.start,
				optimistic_request,
				channel->to_string ());
			}
			else // Pessimistic (safe) request case
			{
				stats.inc (nano::stat::type::bootstrap_request_blocks, nano::stat::detail::safe);

				if (auto conf_info = ledger.store.confirmation_height.get (transaction, account))
				{
					tag.type = query_type::blocks_by_hash;
					tag.start = conf_info->frontier;
					tag.hash = conf_info->frontier;

					logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} starting from confirmation frontier: {} (optimistic: {}) from: {}",
					account,
					tag.start,
					optimistic_request,
					channel->to_string ());
				}
				else
				{
					tag.type = query_type::blocks_by_account;
					tag.start = account;

					logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} starting from account root (optimistic: {}) from: {}",
					account,
					optimistic_request,
					channel->to_string ());
				}
			}
		}
		else
		{
			stats.inc (nano::stat::type::bootstrap_request_blocks, nano::stat::detail::base);

			tag.type = query_type::blocks_by_account;
			tag.start = account;

			logger.debug (nano::log::type::bootstrap, "Requesting blocks for {} from: {}", account, channel->to_string ());
		}
	}

	return send (channel, tag);
}

bool nano::bootstrap_service::request_info (nano::block_hash hash, std::shared_ptr<nano::transport::channel> const & channel, query_source source)
{
	async_tag tag{};
	tag.type = query_type::account_info_by_hash;
	tag.source = source;
	tag.start = hash;
	tag.hash = hash;

	logger.debug (nano::log::type::bootstrap, "Requesting account info for: {} from: {}", hash, channel->to_string ());

	return send (channel, tag);
}

bool nano::bootstrap_service::request_frontiers (nano::account start, std::shared_ptr<nano::transport::channel> const & channel, query_source source)
{
	async_tag tag{};
	tag.type = query_type::frontiers;
	tag.source = source;
	tag.start = start;

	logger.debug (nano::log::type::bootstrap, "Requesting frontiers starting from: {} from: {}", start, channel->to_string ());

	return send (channel, tag);
}

void nano::bootstrap_service::run_one_priority ()
{
	wait_block_processor ();
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto [account, priority, fails] = wait_priority ();
	if (account.is_zero ())
	{
		return;
	}

	// Decide how many blocks to request
	size_t const min_pull_count = 2;
	auto pull_count = std::clamp (static_cast<size_t> (priority), min_pull_count, nano::bootstrap_server::max_blocks);

	bool sent = request (account, pull_count, channel, query_source::priority);

	// Only cooldown accounts that are likely to have more blocks
	// This is to avoid requesting blocks from the same frontier multiple times, before the block processor had a chance to process them
	// Not throttling accounts that are probably up-to-date allows us to evict them from the priority set faster
	if (sent && fails == 0)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		accounts.timestamp_set (account);
	}
}

void nano::bootstrap_service::run_priorities ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop);
		run_one_priority ();
		lock.lock ();
	}
}

void nano::bootstrap_service::run_one_database (bool should_throttle)
{
	wait_block_processor ();
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto account = wait_database (should_throttle);
	if (account.is_zero ())
	{
		return;
	}
	request (account, 2, channel, query_source::database);
}

void nano::bootstrap_service::run_database ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// Avoid high churn rate of database requests
		bool should_throttle = !database_scan.warmed_up () && throttle.throttled ();
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_database);
		run_one_database (should_throttle);
		lock.lock ();
	}
}

void nano::bootstrap_service::run_one_dependency ()
{
	// No need to wait for block_processor, as we are not processing blocks
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto blocking = wait_blocking ();
	if (blocking.is_zero ())
	{
		return;
	}
	request_info (blocking, channel, query_source::dependencies);
}

void nano::bootstrap_service::run_dependencies ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_dependencies);
		run_one_dependency ();
		lock.lock ();
	}
}

void nano::bootstrap_service::run_one_frontier ()
{
	// No need to wait for block_processor, as we are not processing blocks
	wait ([this] () {
		return !accounts.priority_half_full ();
	});
	wait ([this] () {
		return frontiers_limiter.should_pass (1);
	});
	wait ([this] () {
		return workers.queued_tasks () < config.frontier_scan.max_pending;
	});
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto frontier = wait_frontier ();
	if (frontier.is_zero ())
	{
		return;
	}
	request_frontiers (frontier, channel, query_source::frontiers);
}

void nano::bootstrap_service::run_frontiers ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_frontiers);
		run_one_frontier ();
		lock.lock ();
	}
}

void nano::bootstrap_service::cleanup_and_sync ()
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

	// Reinsert known dependencies into the priority set
	if (sync_dependencies_interval.elapse (nano::is_dev_run () ? 500ms : 60s))
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::sync_dependencies);
		accounts.sync_dependencies ();
	}
}

void nano::bootstrap_service::run_timeouts ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::loop_cleanup);
		cleanup_and_sync ();
		condition.wait_for (lock, nano::is_dev_run () ? 500ms : 5s, [this] () { return stopped; });
	}
}

void nano::bootstrap_service::process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel)
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

		bool operator() (const nano::asc_pull_ack::blocks_payload & response) const
		{
			return type == query_type::blocks_by_hash || type == query_type::blocks_by_account;
		}
		bool operator() (const nano::asc_pull_ack::account_info_payload & response) const
		{
			return type == query_type::account_info_by_hash;
		}
		bool operator() (const nano::asc_pull_ack::frontiers_payload & response) const
		{
			return type == query_type::frontiers;
		}
		bool operator() (const nano::empty_payload & response) const
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

	lock.unlock ();

	// Process the response payload
	bool ok = std::visit ([this, &tag] (auto && request) { return process (request, tag); }, message.payload);
	if (ok)
	{
		lock.lock ();
		scoring.received_message (channel);
		lock.unlock ();
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap, nano::stat::detail::invalid_response);
	}

	condition.notify_all ();
}

bool nano::bootstrap_service::process (const nano::asc_pull_ack::blocks_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::blocks_by_hash || tag.type == query_type::blocks_by_account);

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
			if (blocks.front ()->hash () == tag.start.as_block_hash ())
			{
				blocks.pop_front ();
			}

			for (auto const & block : blocks)
			{
				if (block == blocks.back ())
				{
					// It's the last block submitted for this account chain, reset timestamp to allow more requests
					block_processor.add (block, nano::block_source::bootstrap, nullptr, [this, account = tag.account] (auto result) {
						stats.inc (nano::stat::type::bootstrap, nano::stat::detail::timestamp_reset);
						{
							nano::lock_guard<nano::mutex> guard{ mutex };
							accounts.timestamp_reset (account);
						}
						condition.notify_all ();
					});
				}
				else
				{
					block_processor.add (block, nano::block_source::bootstrap);
				}
			}

			if (tag.source == query_source::database)
			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				throttle.add (true);
			}
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_verify_blocks, nano::stat::detail::nothing_new);
			{
				nano::lock_guard<nano::mutex> lock{ mutex };

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

bool nano::bootstrap_service::process (const nano::asc_pull_ack::account_info_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::account_info_by_hash);
	debug_assert (!tag.hash.is_zero ());

	if (response.account.is_zero ())
	{
		stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::account_info_empty);
		return true; // OK, but nothing to do
	}

	stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::account_info);

	// Prioritize account containing the dependency
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		accounts.dependency_update (tag.hash, response.account);
		accounts.priority_set (response.account, nano::bootstrap::account_sets::priority_cutoff); // Use the lowest possible priority here
	}

	return true; // OK, no way to verify the response
}

bool nano::bootstrap_service::process (const nano::asc_pull_ack::frontiers_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::frontiers);
	debug_assert (!tag.start.is_zero ());

	if (response.frontiers.empty ())
	{
		stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::frontiers_empty);
		return true; // OK, but nothing to do
	}

	stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::frontiers);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (nano::stat::type::bootstrap_verify_frontiers, nano::stat::detail::ok);
			stats.add (nano::stat::type::bootstrap, nano::stat::detail::frontiers, nano::stat::dir::in, response.frontiers.size ());

			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				frontiers.process (tag.start.as_account (), response.frontiers);
			}

			// Allow some overfill to avoid unnecessarily dropping responses
			if (workers.queued_tasks () < config.frontier_scan.max_pending * 4)
			{
				workers.post ([this, frontiers = response.frontiers] {
					process_frontiers (frontiers);
				});
			}
			else
			{
				stats.add (nano::stat::type::bootstrap, nano::stat::detail::frontiers_dropped, response.frontiers.size ());
			}
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_verify_frontiers, nano::stat::detail::nothing_new);
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (nano::stat::type::bootstrap_verify_frontiers, nano::stat::detail::invalid);
		}
		break;
	}

	return result != verify_result::invalid;
}

bool nano::bootstrap_service::process (const nano::empty_payload & response, const async_tag & tag)
{
	stats.inc (nano::stat::type::bootstrap_process, nano::stat::detail::empty);
	debug_assert (false, "empty payload"); // Should not happen
	return false; // Invalid
}

void nano::bootstrap_service::process_frontiers (std::deque<std::pair<nano::account, nano::block_hash>> const & frontiers)
{
	release_assert (!frontiers.empty ());

	// Accounts must be passed in ascending order
	debug_assert (std::adjacent_find (frontiers.begin (), frontiers.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs.first.number () >= rhs.first.number ();
	})
	== frontiers.end ());

	stats.inc (nano::stat::type::bootstrap, nano::stat::detail::processing_frontiers);

	size_t outdated = 0;
	size_t pending = 0;

	// Accounts with outdated frontiers to sync
	std::deque<nano::account> result;
	{
		auto transaction = ledger.tx_begin_read ();

		auto const start = frontiers.front ().first;
		nano::bootstrap::account_database_crawler account_crawler{ ledger.store, transaction, start };
		nano::bootstrap::pending_database_crawler pending_crawler{ ledger.store, transaction, start };

		auto block_exists = [&] (nano::block_hash const & hash) {
			return ledger.any.block_exists_or_pruned (transaction, hash);
		};

		auto should_prioritize = [&] (nano::account const & account, nano::block_hash const & frontier) {
			account_crawler.advance_to (account);
			pending_crawler.advance_to (account);

			// Check if account exists in our ledger
			if (account_crawler.current && account_crawler.current->first == account)
			{
				// Check for frontier mismatch
				if (account_crawler.current->second.head != frontier)
				{
					// Check if frontier block exists in our ledger
					if (!block_exists (frontier))
					{
						outdated++;
						return true; // Frontier is outdated
					}
				}
				return false; // Account exists and frontier is up-to-date
			}

			// Check if account has pending blocks in our ledger
			if (pending_crawler.current && pending_crawler.current->first.account == account)
			{
				pending++;
				return true; // Account doesn't exist but has pending blocks in the ledger
			}

			return false; // Account doesn't exist in the ledger and has no pending blocks, can't be prioritized right now
		};

		for (auto const & [account, frontier] : frontiers)
		{
			if (should_prioritize (account, frontier))
			{
				result.push_back (account);
			}
		}
	}

	stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::processed, frontiers.size ());
	stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::prioritized, result.size ());
	stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::outdated, outdated);
	stats.add (nano::stat::type::bootstrap_frontiers, nano::stat::detail::pending, pending);

	logger.debug (nano::log::type::bootstrap, "Processed {} frontiers of which outdated: {}, pending: {}", frontiers.size (), outdated, pending);

	nano::lock_guard<nano::mutex> guard{ mutex };

	for (auto const & account : result)
	{
		// Use the lowest possible priority here
		accounts.priority_set (account, nano::bootstrap::account_sets::priority_cutoff);
	}
}

auto nano::bootstrap_service::verify (const nano::asc_pull_ack::blocks_payload & response, const async_tag & tag) const -> verify_result
{
	auto const & blocks = response.blocks;

	if (blocks.empty ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () == 1 && blocks.front ()->hash () == tag.start.as_block_hash ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () > tag.count)
	{
		return verify_result::invalid;
	}

	auto const & first = blocks.front ();
	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		{
			if (first->hash () != tag.start.as_block_hash ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		case query_type::blocks_by_account:
		{
			// Open & state blocks always contain account field
			if (first->account_field ().value_or (0) != tag.start.as_account ())
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

auto nano::bootstrap_service::verify (nano::asc_pull_ack::frontiers_payload const & response, async_tag const & tag) const -> verify_result
{
	auto const & frontiers = response.frontiers;

	if (frontiers.empty ())
	{
		return verify_result::nothing_new;
	}

	// Ensure frontiers accounts are in ascending order
	nano::account previous{ 0 };
	for (auto const & [account, _] : frontiers)
	{
		if (account.number () <= previous.number ())
		{
			return verify_result::invalid;
		}
		previous = account;
	}

	// Ensure the frontiers are larger or equal to the requested frontier
	if (frontiers.front ().first.number () < tag.start.as_account ().number ())
	{
		return verify_result::invalid;
	}

	return verify_result::ok;
}

auto nano::bootstrap_service::info () const -> nano::bootstrap::account_sets::info_t
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.info ();
}

auto nano::bootstrap_service::status () const -> status_result
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return {
		.priorities = accounts.priority_size (),
		.blocking = accounts.blocked_size (),
	};
}

std::size_t nano::bootstrap_service::compute_throttle_size () const
{
	auto ledger_size = ledger.account_count ();
	size_t target = ledger_size > 0 ? config.throttle_coefficient * static_cast<size_t> (std::log (ledger_size)) : 0;
	size_t min_size = 16;
	return std::max (target, min_size);
}

nano::container_info nano::bootstrap_service::container_info () const
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

/*
 *
 */

nano::stat::detail nano::to_stat_detail (nano::bootstrap_service::query_type type)
{
	return nano::enum_util::cast<nano::stat::detail> (type);
}
