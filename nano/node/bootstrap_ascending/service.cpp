#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>

using namespace std::chrono_literals;

/*
 * bootstrap_ascending
 */

nano::bootstrap_ascending::service::service (nano::node_config const & node_config_a, nano::block_processor & block_processor_a, nano::ledger & ledger_a, nano::network & network_a, nano::stats & stat_a, nano::logger & logger_a) :
	config{ node_config_a.bootstrap_ascending },
	network_constants{ node_config_a.network_params.network },
	block_processor{ block_processor_a },
	ledger{ ledger_a },
	network{ network_a },
	stats{ stat_a },
	logger{ logger_a },
	accounts{ config.account_sets, stats },
	database_scan{ ledger },
	throttle{ compute_throttle_size () },
	scoring{ config, node_config_a.network_params.network },
	database_limiter{ config.database_rate_limit, 1.0 }
{
	// TODO: This is called from a very congested blockprocessor thread. Offload this work to a dedicated processing thread
	block_processor.batch_processed.add ([this] (auto const & batch) {
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

	accounts.priority_set (node_config_a.network_params.ledger.genesis->account_field ().value ());
}

nano::bootstrap_ascending::service::~service ()
{
	// All threads must be stopped before destruction
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!database_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!timeout_thread.joinable ());
}

void nano::bootstrap_ascending::service::start ()
{
	debug_assert (!priorities_thread.joinable ());
	debug_assert (!database_thread.joinable ());
	debug_assert (!dependencies_thread.joinable ());
	debug_assert (!timeout_thread.joinable ());

	if (!config.enable)
	{
		logger.warn (nano::log::type::bootstrap, "Ascending bootstrap is disabled");
		return;
	}

	priorities_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_priorities ();
	});

	if (config.enable_database_scan)
	{
		database_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
			run_database ();
		});
	}

	if (config.enable_dependency_walker)
	{
		dependencies_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
			run_dependencies ();
		});
	}

	timeout_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_timeouts ();
	});
}

void nano::bootstrap_ascending::service::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	nano::join_or_pass (priorities_thread);
	nano::join_or_pass (database_thread);
	nano::join_or_pass (dependencies_thread);
	nano::join_or_pass (timeout_thread);
}

void nano::bootstrap_ascending::service::send (std::shared_ptr<nano::transport::channel> const & channel, async_tag tag)
{
	debug_assert (tag.type != query_type::invalid);
	debug_assert (tag.source != query_source::invalid);

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
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
		default:
			debug_assert (false);
	}

	request.update_header ();

	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::request, nano::stat::dir::out);
	stats.inc (nano::stat::type::bootstrap_ascending_request, to_stat_detail (tag.type));

	// TODO: There is no feedback mechanism if bandwidth limiter starts dropping our requests
	channel->send (
	request, nullptr,
	nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type::bootstrap);
}

std::size_t nano::bootstrap_ascending::service::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.priority_size ();
}

std::size_t nano::bootstrap_ascending::service::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

std::size_t nano::bootstrap_ascending::service::score_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return scoring.size ();
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap_ascending::service::inspect (secure::transaction const & tx, nano::block_status const & result, nano::block const & block, nano::block_source source)
{
	debug_assert (!mutex.try_lock ());

	auto const hash = block.hash ();

	switch (result)
	{
		case nano::block_status::progress:
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
		break;
		case nano::block_status::gap_source:
		{
			if (source == nano::block_source::bootstrap)
			{
				const auto account = block.previous ().is_zero () ? block.account_field ().value () : ledger.any.block_account (tx, block.previous ()).value ();
				const auto source_hash = block.source_field ().value_or (block.link_field ().value_or (0).as_block_hash ());

				// Mark account as blocked because it is missing the source block
				accounts.block (account, source_hash);
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
			break;
	}
}

void nano::bootstrap_ascending::service::wait (std::function<bool ()> const & predicate) const
{
	std::unique_lock<nano::mutex> lock{ mutex };

	std::chrono::milliseconds interval = 5ms;
	while (!stopped && !predicate ())
	{
		condition.wait_for (lock, interval);
		interval = std::min (interval * 2, config.throttle_wait);
	}
}

void nano::bootstrap_ascending::service::wait_tags () const
{
	wait ([this] () {
		debug_assert (!mutex.try_lock ());
		return tags.size () < config.max_requests;
	});
}

void nano::bootstrap_ascending::service::wait_blockprocessor () const
{
	wait ([this] () {
		return block_processor.size (nano::block_source::bootstrap) < config.block_processor_threshold;
	});
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::service::wait_channel ()
{
	std::shared_ptr<nano::transport::channel> channel;

	wait ([this, &channel] () {
		debug_assert (!mutex.try_lock ());
		channel = scoring.channel ();
		return channel != nullptr; // Wait until a channel is available
	});

	return channel;
}

size_t nano::bootstrap_ascending::service::count_tags (nano::account const & account, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_account> ().equal_range (account);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

size_t nano::bootstrap_ascending::service::count_tags (nano::block_hash const & hash, query_source source) const
{
	debug_assert (!mutex.try_lock ());
	auto [begin, end] = tags.get<tag_hash> ().equal_range (hash);
	return std::count_if (begin, end, [source] (auto const & tag) { return tag.source == source; });
}

std::pair<nano::account, double> nano::bootstrap_ascending::service::next_priority ()
{
	debug_assert (!mutex.try_lock ());

	auto account = accounts.next_priority ([this] (nano::account const & account) {
		return count_tags (account, query_source::priority) < 4;
	});

	if (account.is_zero ())
	{
		return {};
	}

	stats.inc (nano::stat::type::bootstrap_ascending_next, nano::stat::detail::next_priority);
	accounts.timestamp_set (account);

	// TODO: Priority could be returned by the accounts.next_priority() call
	return { account, accounts.priority (account) };
}

std::pair<nano::account, double> nano::bootstrap_ascending::service::wait_priority ()
{
	std::pair<nano::account, double> result{ 0, 0 };

	wait ([this, &result] () {
		debug_assert (!mutex.try_lock ());
		result = next_priority ();
		if (!result.first.is_zero ())
		{
			return true;
		}
		return false;
	});

	return result;
}

nano::account nano::bootstrap_ascending::service::next_database (bool should_throttle)
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

	stats.inc (nano::stat::type::bootstrap_ascending_next, nano::stat::detail::next_database);
	return account;
}

nano::account nano::bootstrap_ascending::service::wait_database (bool should_throttle)
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

nano::block_hash nano::bootstrap_ascending::service::next_blocking ()
{
	debug_assert (!mutex.try_lock ());

	auto blocking = accounts.next_blocking ([this] (nano::block_hash const & hash) {
		return count_tags (hash, query_source::blocking) == 0;
	});

	if (blocking.is_zero ())
	{
		return { 0 };
	}

	stats.inc (nano::stat::type::bootstrap_ascending_next, nano::stat::detail::next_blocking);
	return blocking;
}

nano::block_hash nano::bootstrap_ascending::service::wait_blocking ()
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

bool nano::bootstrap_ascending::service::request (nano::account account, size_t count, std::shared_ptr<nano::transport::channel> const & channel, query_source source)
{
	debug_assert (count > 0);
	debug_assert (count <= nano::bootstrap_server::max_blocks);

	// Limit the max number of blocks to pull
	count = std::min (count, config.max_pull_count);

	async_tag tag{};
	tag.source = source;
	tag.account = account;
	tag.count = count;

	// Check if the account picked has blocks, if it does, start the pull from the highest block
	auto info = ledger.store.account.get (ledger.store.tx_begin_read (), account);
	if (info)
	{
		tag.type = query_type::blocks_by_hash;
		tag.start = info->head;
		tag.hash = info->head;
	}
	else
	{
		tag.type = query_type::blocks_by_account;
		tag.start = account;
	}

	on_request.notify (tag, channel);

	send (channel, tag);

	return true; // Request sent
}

bool nano::bootstrap_ascending::service::request_info (nano::block_hash hash, std::shared_ptr<nano::transport::channel> const & channel, query_source source)
{
	async_tag tag{};
	tag.type = query_type::account_info_by_hash;
	tag.source = source;
	tag.start = hash;
	tag.hash = hash;

	on_request.notify (tag, channel);

	send (channel, tag);

	return true; // Request sent
}

void nano::bootstrap_ascending::service::run_one_priority ()
{
	wait_tags ();
	wait_blockprocessor ();
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto [account, priority] = wait_priority ();
	if (account.is_zero ())
	{
		return;
	}
	size_t const min_pull_count = 2;
	auto count = std::clamp (static_cast<size_t> (priority), min_pull_count, nano::bootstrap_server::max_blocks);
	request (account, count, channel, query_source::priority);
}

void nano::bootstrap_ascending::service::run_priorities ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop);
		run_one_priority ();
		lock.lock ();
	}
}

void nano::bootstrap_ascending::service::run_one_database (bool should_throttle)
{
	wait_tags ();
	wait_blockprocessor ();
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

void nano::bootstrap_ascending::service::run_database ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// Avoid high churn rate of database requests
		bool should_throttle = !database_scan.warmed_up () && throttle.throttled ();
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop_database);
		run_one_database (should_throttle);
		lock.lock ();
	}
}

void nano::bootstrap_ascending::service::run_one_blocking ()
{
	wait_tags ();
	wait_blockprocessor ();
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
	request_info (blocking, channel, query_source::blocking);
}

void nano::bootstrap_ascending::service::run_dependencies ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop_dependencies);
		run_one_blocking ();
		lock.lock ();
	}
}

void nano::bootstrap_ascending::service::cleanup_and_sync ()
{
	debug_assert (!mutex.try_lock ());

	scoring.sync (network.list ());
	scoring.timeout ();

	throttle.resize (compute_throttle_size ());

	auto const cutoff = std::chrono::steady_clock::now () - config.request_timeout;
	auto should_timeout = [cutoff] (async_tag const & tag) {
		return tag.timestamp < cutoff;
	};

	auto & tags_by_order = tags.get<tag_sequenced> ();
	while (!tags_by_order.empty () && should_timeout (tags_by_order.front ()))
	{
		auto tag = tags_by_order.front ();
		tags_by_order.pop_front ();
		on_timeout.notify (tag);
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::timeout);
	}

	if (sync_dependencies_interval.elapsed (60s))
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::sync_dependencies);
		accounts.sync_dependencies ();
	}
}

void nano::bootstrap_ascending::service::run_timeouts ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop_cleanup);
		cleanup_and_sync ();
		condition.wait_for (lock, 5s, [this] () { return stopped; });
	}
}

void nano::bootstrap_ascending::service::process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// Only process messages that have a known tag
	auto it = tags.get<tag_id> ().find (message.id);
	if (it == tags.get<tag_id> ().end ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::missing_tag);
		return;
	}

	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::reply);

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
			return false; // TODO: Handle frontiers
		}
		bool operator() (const nano::empty_payload & response) const
		{
			return false; // Should not happen
		}
	};

	bool valid = std::visit (payload_verifier{ tag.type }, message.payload);
	if (!valid)
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::invalid_response_type);
		return;
	}

	// Track bootstrap request response time
	stats.inc (nano::stat::type::bootstrap_ascending_reply, to_stat_detail (tag.type));
	stats.sample (nano::stat::sample::bootstrap_tag_duration, nano::log::milliseconds_delta (tag.timestamp), { 0, config.request_timeout.count () });

	scoring.received_message (channel);

	lock.unlock ();

	on_reply.notify (tag);

	// Process the response payload
	std::visit ([this, &tag] (auto && request) { return process (request, tag); }, message.payload);

	condition.notify_all ();
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::blocks_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::blocks_by_hash || tag.type == query_type::blocks_by_account);

	stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::blocks);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (nano::stat::type::bootstrap_ascending_verify, nano::stat::detail::ok);
			stats.add (nano::stat::type::bootstrap_ascending, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

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
						stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::timestamp_reset);
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
			stats.inc (nano::stat::type::bootstrap_ascending_verify, nano::stat::detail::nothing_new);

			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.priority_down (tag.account);
			if (tag.source == query_source::database)
			{
				throttle.add (false);
			}
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (nano::stat::type::bootstrap_ascending_verify, nano::stat::detail::invalid);
		}
		break;
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::account_info_payload & response, const async_tag & tag)
{
	debug_assert (tag.type == query_type::account_info_by_hash);
	debug_assert (!tag.hash.is_zero ());

	if (response.account.is_zero ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::account_info_empty);
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::account_info);

		// Prioritize account containing the dependency
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.dependency_update (tag.hash, response.account);
			accounts.priority_set (response.account);
		}
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::frontiers_payload & response, const async_tag & tag)
{
	// TODO: Make use of frontiers info
	stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::frontiers);
}

void nano::bootstrap_ascending::service::process (const nano::empty_payload & response, const async_tag & tag)
{
	stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::empty);
	debug_assert (false, "empty payload"); // Should not happen
}

nano::bootstrap_ascending::service::verify_result nano::bootstrap_ascending::service::verify (const nano::asc_pull_ack::blocks_payload & response, const nano::bootstrap_ascending::service::async_tag & tag) const
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
			if (first->account_field () != tag.start.as_account ())
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

auto nano::bootstrap_ascending::service::info () const -> nano::bootstrap_ascending::account_sets::info_t
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.info ();
}

std::size_t nano::bootstrap_ascending::service::compute_throttle_size () const
{
	auto ledger_size = ledger.account_count ();
	size_t target = ledger_size > 0 ? config.throttle_coefficient * static_cast<size_t> (std::log (ledger_size)) : 0;
	size_t min_size = 16;
	return std::max (target, min_size);
}

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::service::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "tags", tags.size (), sizeof (decltype (tags)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle", throttle.size (), 0 }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle_successes", throttle.successes (), 0 }));
	composite->add_component (accounts.collect_container_info ("accounts"));
	composite->add_component (database_scan.collect_container_info ("database_scan"));
	return composite;
}

/*
 *
 */

nano::stat::detail nano::bootstrap_ascending::to_stat_detail (nano::bootstrap_ascending::service::query_type type)
{
	return nano::enum_util::cast<nano::stat::detail> (type);
}