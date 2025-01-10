#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/stats_enums.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/node/block_processor.hpp>
#include <celerix/node/bootstrap/bootstrap_service.hpp>
#include <celerix/node/bootstrap/crawlers.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/transport/transport.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/confirmation_height.hpp>

using namespace std::chrono_literals;

celerix::bootstrap_service::bootstrap_service (celerix::node_config const & node_config_a, celerix::block_processor & block_processor_a, celerix::ledger & ledger_a, celerix::network & network_a, celerix::stats & stat_a, celerix::logger & logger_a) :
	config{ node_config_a.bootstrap },
	network_constants{ node_config_a.network_params.network },
	block_processor{ block_processor_a },
	ledger{ ledger_a },
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
	workers{ 1, celerix::thread_role::name::bootstrap_worker }
{
	block_processor.batch_processed.add ([this] (auto const & batch) {
		{
			celerix::lock_guard<celerix::mutex> lock{ mutex };

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
	block_processor.rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		for (auto const & block : blocks)
		{
			debug_assert (block != nullptr);
			accounts.unblock (block->account ());
		}
	});

	accounts.priority_set (node_config_a.network_params.ledger.genesis->account_field ().value ());
}

celerix::bootstrap_service::~bootstrap_service ()
{
	// All threads must be stopped before destruction
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!database_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!frontiers_thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());
	debug_assert (!workers.alive ());
}

void celerix::bootstrap_service::start ()
{
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!database_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!frontiers_thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());

	if (!config.enable)
	{
		logger.warn (celerix::log::type::bootstrap, "Bootstrap is disabled, node will not be able to synchronize with the network");
		return;
	}

	workers.start ();

	if (config.enable_scan)
	{
		priorities_thread = std::thread ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::bootstrap);
			run_priorities ();
		});
	}

	if (config.enable_database_scan)
	{
		database_thread = std::thread ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::bootstrap_database_scan);
			run_database ();
		});
	}

	if (config.enable_dependency_walker)
	{
		dependencies_thread = std::thread ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::bootstrap_dependency_walker);
			run_dependencies ();
		});
	}

	if (config.enable_frontier_scan)
	{
		frontiers_thread = std::thread ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::bootstrap_frontier_scan);
			run_frontiers ();
		});
	}

	cleanup_thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::bootstrap_cleanup);
		run_timeouts ();
	});
}

void celerix::bootstrap_service::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	celerix::join_or_pass (priorities_thread);
	celerix::join_or_pass (database_thread);
	celerix::join_or_pass (dependencies_thread);
	celerix::join_or_pass (frontiers_thread);
	celerix::join_or_pass (cleanup_thread);

	workers.stop ();
}

bool celerix::bootstrap_service::send (std::shared_ptr<celerix::transport::channel> const & channel, async_tag tag)
{
	debug_assert (tag.type != query_type::invalid);
	debug_assert (tag.source != query_source::invalid);

	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
		// Give extra time for the request to be processed by the channel
		tag.cutoff = std::chrono::steady_clock::now () + config.request_timeout * 4;
		tags.get<tag_id> ().insert (tag);
	}

	celerix::asc_pull_req request{ network_constants };
	request.id = tag.id;

	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		case query_type::blocks_by_account:
		{
			request.type = celerix::asc_pull_type::blocks;

			celerix::asc_pull_req::blocks_payload pld;
			pld.start = tag.start;
			pld.count = tag.count;
			pld.start_type = tag.type == query_type::blocks_by_hash ? celerix::asc_pull_req::hash_type::block : celerix::asc_pull_req::hash_type::account;
			request.payload = pld;
		}
		break;
		case query_type::account_info_by_hash:
		{
			request.type = celerix::asc_pull_type::account_info;

			celerix::asc_pull_req::account_info_payload pld;
			pld.target_type = celerix::asc_pull_req::hash_type::block; // Query account info by block hash
			pld.target = tag.start;
			request.payload = pld;
		}
		break;
		case query_type::frontiers:
		{
			request.type = celerix::asc_pull_type::frontiers;

			celerix::asc_pull_req::frontiers_payload pld;
			pld.start = tag.start.as_account ();
			pld.count = celerix::asc_pull_ack::frontiers_payload::max_frontiers;
			request.payload = pld;
		}
		break;
		default:
			debug_assert (false);
	}

	request.update_header ();

	bool sent = channel->send (
	request, celerix::transport::traffic_type::bootstrap_requests, [this, id = tag.id] (auto const & ec, auto size) {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		if (auto it = tags.get<tag_id> ().find (id); it != tags.get<tag_id> ().end ())
		{
			stats.inc (celerix::stat::type::bootstrap_request_ec, to_stat_detail (ec), celerix::stat::dir::out);
			if (!ec)
			{
				stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::request_success, celerix::stat::dir::out);
				tags.get<tag_id> ().modify (it, [&] (auto & tag) {
					// After the request has been sent, the peer has a limited time to respond
					tag.cutoff = std::chrono::steady_clock::now () + config.request_timeout;
				});
			}
			else
			{
				stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::request_failed, celerix::stat::dir::out);
				tags.get<tag_id> ().erase (it);
			}
		} });

	if (sent)
	{
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::request);
		stats.inc (celerix::stat::type::bootstrap_request, to_stat_detail (tag.type));
	}
	else
	{
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::request_failed);
	}

	return sent;
}

std::size_t celerix::bootstrap_service::priority_size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return accounts.priority_size ();
}

std::size_t celerix::bootstrap_service::blocked_size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

std::size_t celerix::bootstrap_service::score_size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return scoring.size ();
}

bool celerix::bootstrap_service::prioritized (celerix::account const & account) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return accounts.prioritized (account);
}

bool celerix::bootstrap_service::blocked (celerix::account const & account) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return accounts.blocked (account);
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void celerix::bootstrap_service::inspect (secure::transaction const & tx, celerix::block_status const & result, celerix::block const & block, celerix::block_source source)
{
	debug_assert (!mutex.try_lock ());

	auto const hash = block.hash ();

	switch (result)
	{
		case celerix::block_status::progress:
		{
			// Progress blocks from live traffic don't need further bootstrapping
			if (source != celerix::block_source::live)
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
		case celerix::block_status::gap_source:
		{
			// Prevent malicious live traffic from filling up the blocked set
			if (source == celerix::block_source::bootstrap)
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
		case celerix::block_status::gap_previous:
		{
			// Prevent live traffic from evicting accounts from the priority list
			if (source == celerix::block_source::live && !accounts.priority_half_full () && !accounts.blocked_half_full ())
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
			break;
	}
}

void celerix::bootstrap_service::wait (std::function<bool ()> const & predicate) const
{
	std::unique_lock<celerix::mutex> lock{ mutex };
	std::chrono::milliseconds interval = 5ms;
	while (!stopped && !predicate ())
	{
		condition.wait_for (lock, interval);
		interval = std::min (interval * 2, config.throttle_wait);
	}
}

void celerix::bootstrap_service::wait_block_processor () const
{
	wait ([this] () {
		return block_processor.size (celerix::block_source::bootstrap) < config.block_processor_threshold;
	});
}

std::shared_ptr<celerix::transport::channel> celerix::bootstrap_service::wait_channel ()
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
	std::shared_ptr<celerix::transport::channel> channel;
	wait ([this, &channel] () {
		channel = scoring.channel ();
		return channel != nullptr; // Wait until a channel is available
	});
	return channel;
}

size_t celerix::bootstrap_service::count_tags (celerix::account const & account, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_account> ().equal_range (account);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

size_t celerix::bootstrap_service::count_tags (celerix::block_hash const & hash, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_hash> ().equal_range (hash);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

celerix::bootstrap::account_sets::priority_result celerix::bootstrap_service::next_priority ()
{
	debug_assert (!mutex.try_lock ());

	auto next = accounts.next_priority ([this] (celerix::account const & account) {
		return count_tags (account, query_source::priority) < 4;
	});
	if (next.account.is_zero ())
	{
		return {};
	}
	stats.inc (celerix::stat::type::bootstrap_next, celerix::stat::detail::next_priority);
	return next;
}

celerix::bootstrap::account_sets::priority_result celerix::bootstrap_service::wait_priority ()
{
	celerix::bootstrap::account_sets::priority_result result{};
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

celerix::account celerix::bootstrap_service::next_database (bool should_throttle)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (config.database_warmup_ratio > 0);

	// Throttling increases the weight of database requests
	if (!database_limiter.should_pass (should_throttle ? config.database_warmup_ratio : 1))
	{
		return { 0 };
	}
	auto account = database_scan.next ([this] (celerix::account const & account) {
		return count_tags (account, query_source::database) == 0;
	});
	if (account.is_zero ())
	{
		return { 0 };
	}
	stats.inc (celerix::stat::type::bootstrap_next, celerix::stat::detail::next_database);
	return account;
}

celerix::account celerix::bootstrap_service::wait_database (bool should_throttle)
{
	celerix::account result{ 0 };
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

celerix::block_hash celerix::bootstrap_service::next_blocking ()
{
	debug_assert (!mutex.try_lock ());

	auto blocking = accounts.next_blocking ([this] (celerix::block_hash const & hash) {
		return count_tags (hash, query_source::dependencies) == 0;
	});
	if (blocking.is_zero ())
	{
		return { 0 };
	}
	stats.inc (celerix::stat::type::bootstrap_next, celerix::stat::detail::next_blocking);
	return blocking;
}

celerix::block_hash celerix::bootstrap_service::wait_blocking ()
{
	celerix::block_hash result{ 0 };
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

celerix::account celerix::bootstrap_service::wait_frontier ()
{
	celerix::account result{ 0 };
	wait ([this, &result] () {
		debug_assert (!mutex.try_lock ());
		result = frontiers.next ();
		if (!result.is_zero ())
		{
			stats.inc (celerix::stat::type::bootstrap_next, celerix::stat::detail::next_frontier);
			return true;
		}
		return false;
	});
	return result;
}

bool celerix::bootstrap_service::request (celerix::account account, size_t count, std::shared_ptr<celerix::transport::channel> const & channel, query_source source)
{
	debug_assert (count > 0);
	debug_assert (count <= celerix::bootstrap_server::max_blocks);

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
			tag.type = query_type::blocks_by_hash;

			// Probabilistically choose between requesting blocks from account frontier or confirmed frontier
			// Optimistic requests start from the (possibly unconfirmed) account frontier and are vulnerable to bootstrap poisoning
			// Safe requests start from the confirmed frontier and given enough time will eventually resolve forks
			bool optimistic_reuest = rng.random (100) < config.optimistic_request_percentage;
			if (!optimistic_reuest)
			{
				if (auto conf_info = ledger.store.confirmation_height.get (transaction, account))
				{
					stats.inc (celerix::stat::type::bootstrap_request_blocks, celerix::stat::detail::safe);
					tag.start = conf_info->frontier;
					tag.hash = conf_info->height;
				}
			}
			if (tag.start.is_zero ())
			{
				stats.inc (celerix::stat::type::bootstrap_request_blocks, celerix::stat::detail::optimistic);
				tag.start = info->head;
				tag.hash = info->head;
			}
		}
		else
		{
			stats.inc (celerix::stat::type::bootstrap_request_blocks, celerix::stat::detail::base);
			tag.type = query_type::blocks_by_account;
			tag.start = account;
		}
	}

	return send (channel, tag);
}

bool celerix::bootstrap_service::request_info (celerix::block_hash hash, std::shared_ptr<celerix::transport::channel> const & channel, query_source source)
{
	async_tag tag{};
	tag.type = query_type::account_info_by_hash;
	tag.source = source;
	tag.start = hash;
	tag.hash = hash;
	return send (channel, tag);
}

bool celerix::bootstrap_service::request_frontiers (celerix::account start, std::shared_ptr<celerix::transport::channel> const & channel, query_source source)
{
	async_tag tag{};
	tag.type = query_type::frontiers;
	tag.source = source;
	tag.start = start;
	return send (channel, tag);
}

void celerix::bootstrap_service::run_one_priority ()
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
	auto pull_count = std::clamp (static_cast<size_t> (priority), min_pull_count, celerix::bootstrap_server::max_blocks);

	bool sent = request (account, pull_count, channel, query_source::priority);

	// Only cooldown accounts that are likely to have more blocks
	// This is to avoid requesting blocks from the same frontier multiple times, before the block processor had a chance to process them
	// Not throttling accounts that are probably up-to-date allows us to evict them from the priority set faster
	if (sent && fails == 0)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		accounts.timestamp_set (account);
	}
}

void celerix::bootstrap_service::run_priorities ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::loop);
		run_one_priority ();
		lock.lock ();
	}
}

void celerix::bootstrap_service::run_one_database (bool should_throttle)
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

void celerix::bootstrap_service::run_database ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		// Avoid high churn rate of database requests
		bool should_throttle = !database_scan.warmed_up () && throttle.throttled ();
		lock.unlock ();
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::loop_database);
		run_one_database (should_throttle);
		lock.lock ();
	}
}

void celerix::bootstrap_service::run_one_dependency ()
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

void celerix::bootstrap_service::run_dependencies ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::loop_dependencies);
		run_one_dependency ();
		lock.lock ();
	}
}

void celerix::bootstrap_service::run_one_frontier ()
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

void celerix::bootstrap_service::run_frontiers ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::loop_frontiers);
		run_one_frontier ();
		lock.lock ();
	}
}

void celerix::bootstrap_service::cleanup_and_sync ()
{
	debug_assert (!mutex.try_lock ());

	scoring.sync (network.list (/* all */ 0, network_constants.bootstrap_protocol_version_min));
	scoring.timeout ();

	throttle.resize (compute_throttle_size ());

	auto const now = std::chrono::steady_clock::now ();
	auto should_timeout = [&] (async_tag const & tag) {
		return tag.cutoff < now;
	};

	auto & tags_by_order = tags.get<tag_sequenced> ();
	while (!tags_by_order.empty () && should_timeout (tags_by_order.front ()))
	{
		auto tag = tags_by_order.front ();
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::timeout);
		stats.inc (celerix::stat::type::bootstrap_timeout, to_stat_detail (tag.type));
		tags_by_order.pop_front ();
	}

	if (sync_dependencies_interval.elapsed (60s))
	{
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::sync_dependencies);
		accounts.sync_dependencies ();
	}
}

void celerix::bootstrap_service::run_timeouts ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::loop_cleanup);
		cleanup_and_sync ();
		condition.wait_for (lock, 5s, [this] () { return stopped; });
	}
}

void celerix::bootstrap_service::process (celerix::asc_pull_ack const & message, std::shared_ptr<celerix::transport::channel> const & channel)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };

	// Only process messages that have a known tag
	auto it = tags.get<tag_id> ().find (message.id);
	if (it == tags.get<tag_id> ().end ())
	{
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::missing_tag);
		return;
	}

	stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::reply);

	auto tag = *it;
	tags.get<tag_id> ().erase (it); // Iterator is invalid after this point

	// Verifies that response type corresponds to our query
	struct payload_verifier
	{
		query_type type;

		bool operator() (const celerix::asc_pull_ack::blocks_payload & response) const
		{
			return type == query_type::blocks_by_hash || type == query_type::blocks_by_account;
		}
		bool operator() (const celerix::asc_pull_ack::account_info_payload & response) const
		{
			return type == query_type::account_info_by_hash;
		}
		bool operator() (const celerix::asc_pull_ack::frontiers_payload & response) const
		{
			return type == query_type::frontiers;
		}
		bool operator() (const celerix::empty_payload & response) const
		{
			return false; // Should not happen
		}
	};

	bool valid = std::visit (payload_verifier{ tag.type }, message.payload);
	if (!valid)
	{
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::invalid_response_type);
		return;
	}

	// Track bootstrap request response time
	stats.inc (celerix::stat::type::bootstrap_reply, to_stat_detail (tag.type));
	stats.sample (celerix::stat::sample::bootstrap_tag_duration, celerix::log::milliseconds_delta (tag.timestamp), { 0, config.request_timeout.count () });

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
		stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::invalid_response);
	}

	condition.notify_all ();
}

bool celerix::bootstrap_service::process (const celerix::asc_pull_ack::blocks_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::blocks_by_hash || tag.type == query_type::blocks_by_account);

	stats.inc (celerix::stat::type::bootstrap_process, celerix::stat::detail::blocks);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (celerix::stat::type::bootstrap_verify_blocks, celerix::stat::detail::ok);
			stats.add (celerix::stat::type::bootstrap, celerix::stat::detail::blocks, celerix::stat::dir::in, response.blocks.size ());

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
					block_processor.add (block, celerix::block_source::bootstrap, nullptr, [this, account = tag.account] (auto result) {
						stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::timestamp_reset);
						{
							celerix::lock_guard<celerix::mutex> guard{ mutex };
							accounts.timestamp_reset (account);
						}
						condition.notify_all ();
					});
				}
				else
				{
					block_processor.add (block, celerix::block_source::bootstrap);
				}
			}

			if (tag.source == query_source::database)
			{
				celerix::lock_guard<celerix::mutex> lock{ mutex };
				throttle.add (true);
			}
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (celerix::stat::type::bootstrap_verify_blocks, celerix::stat::detail::nothing_new);
			{
				celerix::lock_guard<celerix::mutex> lock{ mutex };

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
			stats.inc (celerix::stat::type::bootstrap_verify_blocks, celerix::stat::detail::invalid);
		}
		break;
	}

	return result != verify_result::invalid;
}

bool celerix::bootstrap_service::process (const celerix::asc_pull_ack::account_info_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::account_info_by_hash);
	debug_assert (!tag.hash.is_zero ());

	if (response.account.is_zero ())
	{
		stats.inc (celerix::stat::type::bootstrap_process, celerix::stat::detail::account_info_empty);
		return true; // OK, but nothing to do
	}

	stats.inc (celerix::stat::type::bootstrap_process, celerix::stat::detail::account_info);

	// Prioritize account containing the dependency
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		accounts.dependency_update (tag.hash, response.account);
		accounts.priority_set (response.account, celerix::bootstrap::account_sets::priority_cutoff); // Use the lowest possible priority here
	}

	return true; // OK, no way to verify the response
}

bool celerix::bootstrap_service::process (const celerix::asc_pull_ack::frontiers_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::frontiers);
	debug_assert (!tag.start.is_zero ());

	if (response.frontiers.empty ())
	{
		stats.inc (celerix::stat::type::bootstrap_process, celerix::stat::detail::frontiers_empty);
		return true; // OK, but nothing to do
	}

	stats.inc (celerix::stat::type::bootstrap_process, celerix::stat::detail::frontiers);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (celerix::stat::type::bootstrap_verify_frontiers, celerix::stat::detail::ok);
			stats.add (celerix::stat::type::bootstrap, celerix::stat::detail::frontiers, celerix::stat::dir::in, response.frontiers.size ());

			{
				celerix::lock_guard<celerix::mutex> lock{ mutex };
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
				stats.add (celerix::stat::type::bootstrap, celerix::stat::detail::frontiers_dropped, response.frontiers.size ());
			}
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (celerix::stat::type::bootstrap_verify_frontiers, celerix::stat::detail::nothing_new);
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (celerix::stat::type::bootstrap_verify_frontiers, celerix::stat::detail::invalid);
		}
		break;
	}

	return result != verify_result::invalid;
}

bool celerix::bootstrap_service::process (const celerix::empty_payload & response, const async_tag & tag)
{
	stats.inc (celerix::stat::type::bootstrap_process, celerix::stat::detail::empty);
	debug_assert (false, "empty payload"); // Should not happen
	return false; // Invalid
}

void celerix::bootstrap_service::process_frontiers (std::deque<std::pair<celerix::account, celerix::block_hash>> const & frontiers)
{
	release_assert (!frontiers.empty ());

	// Accounts must be passed in ascending order
	debug_assert (std::adjacent_find (frontiers.begin (), frontiers.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs.first.number () >= rhs.first.number ();
	})
	== frontiers.end ());

	stats.inc (celerix::stat::type::bootstrap, celerix::stat::detail::processing_frontiers);

	size_t outdated = 0;
	size_t pending = 0;

	// Accounts with outdated frontiers to sync
	std::deque<celerix::account> result;
	{
		auto transaction = ledger.tx_begin_read ();

		auto const start = frontiers.front ().first;
		celerix::bootstrap::account_database_crawler account_crawler{ ledger.store, transaction, start };
		celerix::bootstrap::pending_database_crawler pending_crawler{ ledger.store, transaction, start };

		auto block_exists = [&] (celerix::block_hash const & hash) {
			return ledger.any.block_exists_or_pruned (transaction, hash);
		};

		auto should_prioritize = [&] (celerix::account const & account, celerix::block_hash const & frontier) {
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

	stats.add (celerix::stat::type::bootstrap_frontiers, celerix::stat::detail::processed, frontiers.size ());
	stats.add (celerix::stat::type::bootstrap_frontiers, celerix::stat::detail::prioritized, result.size ());
	stats.add (celerix::stat::type::bootstrap_frontiers, celerix::stat::detail::outdated, outdated);
	stats.add (celerix::stat::type::bootstrap_frontiers, celerix::stat::detail::pending, pending);

	celerix::lock_guard<celerix::mutex> guard{ mutex };

	for (auto const & account : result)
	{
		// Use the lowest possible priority here
		accounts.priority_set (account, celerix::bootstrap::account_sets::priority_cutoff);
	}
}

auto celerix::bootstrap_service::verify (const celerix::asc_pull_ack::blocks_payload & response, const async_tag & tag) const -> verify_result
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
	celerix::block_hash previous_hash = blocks.front ()->hash ();
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

auto celerix::bootstrap_service::verify (celerix::asc_pull_ack::frontiers_payload const & response, async_tag const & tag) const -> verify_result
{
	auto const & frontiers = response.frontiers;

	if (frontiers.empty ())
	{
		return verify_result::nothing_new;
	}

	// Ensure frontiers accounts are in ascending order
	celerix::account previous{ 0 };
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

auto celerix::bootstrap_service::info () const -> celerix::bootstrap::account_sets::info_t
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return accounts.info ();
}

std::size_t celerix::bootstrap_service::compute_throttle_size () const
{
	auto ledger_size = ledger.account_count ();
	size_t target = ledger_size > 0 ? config.throttle_coefficient * static_cast<size_t> (std::log (ledger_size)) : 0;
	size_t min_size = 16;
	return std::max (target, min_size);
}

celerix::container_info celerix::bootstrap_service::container_info () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	auto collect_limiters = [this] () {
		celerix::container_info info;
		info.put ("total", limiter.size ());
		info.put ("database", database_limiter.size ());
		info.put ("frontiers", frontiers_limiter.size ());
		return info;
	};

	celerix::container_info info;
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

celerix::stat::detail celerix::to_stat_detail (celerix::bootstrap_service::query_type type)
{
	return celerix::enum_util::cast<celerix::stat::detail> (type);
}
