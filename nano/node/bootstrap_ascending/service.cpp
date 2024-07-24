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

nano::bootstrap_ascending::service::service (nano::node_config & config_a, nano::block_processor & block_processor_a, nano::ledger & ledger_a, nano::network & network_a, nano::stats & stat_a) :
	config{ config_a },
	network_consts{ config.network_params.network },
	block_processor{ block_processor_a },
	ledger{ ledger_a },
	network{ network_a },
	stats{ stat_a },
	accounts{ stats },
	iterator{ ledger },
	throttle{ compute_throttle_size () },
	scoring{ config.bootstrap_ascending, config.network_params.network },
	database_limiter{ config.bootstrap_ascending.database_requests_limit, 1.0 }
{
	// TODO: This is called from a very congested blockprocessor thread. Offload this work to a dedicated processing thread
	block_processor.batch_processed.add ([this] (auto const & batch) {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };

			auto transaction = ledger.tx_begin_read ();
			for (auto const & [result, context] : batch)
			{
				debug_assert (context.block != nullptr);
				inspect (transaction, result, *context.block);
			}
		}

		condition.notify_all ();
	});
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

	priorities_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_priorities ();
	});

	database_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_database ();
	});

	dependencies_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_dependencies ();
	});

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

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
		tags.get<tag_id> ().insert (tag);
	}

	nano::asc_pull_req request{ network_consts };
	request.id = tag.id;

	switch (tag.type)
	{
		case query_type::blocks_by_hash:
		case query_type::blocks_by_account:
		{
			request.type = nano::asc_pull_type::blocks;

			nano::asc_pull_req::blocks_payload pld;
			pld.start = tag.start;
			pld.count = config.bootstrap_ascending.pull_count;
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
void nano::bootstrap_ascending::service::inspect (secure::transaction const & tx, nano::block_status const & result, nano::block const & block)
{
	auto const hash = block.hash ();

	switch (result)
	{
		case nano::block_status::progress:
		{
			const auto account = block.account ();

			// If we've inserted any block in to an account, unmark it as blocked
			accounts.unblock (account);
			accounts.priority_up (account);
			accounts.timestamp_reset (account);

			if (block.is_send ())
			{
				auto destination = block.destination ();
				accounts.unblock (destination, hash); // Unblocking automatically inserts account into priority set
				accounts.priority_up (destination);
			}
		}
		break;
		case nano::block_status::gap_source:
		{
			const auto account = block.previous ().is_zero () ? block.account_field ().value () : ledger.any.block_account (tx, block.previous ()).value ();
			const auto source = block.source_field ().value_or (block.link_field ().value_or (0).as_block_hash ());

			// Mark account as blocked because it is missing the source block
			accounts.block (account, source);

			// TODO: Track stats
		}
		break;
		case nano::block_status::old:
		{
			// TODO: Track stats
		}
		break;
		case nano::block_status::gap_previous:
		{
			// TODO: Track stats
		}
		break;
		default: // No need to handle other cases
			break;
	}
}

void nano::bootstrap_ascending::service::wait_blockprocessor ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped && block_processor.size (nano::block_source::bootstrap) > config.bootstrap_ascending.block_wait_count)
	{
		condition.wait_for (lock, std::chrono::milliseconds{ config.bootstrap_ascending.throttle_wait }, [this] () { return stopped; }); // Blockprocessor is relatively slow, sleeping here instead of using conditions
	}
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::service::wait_channel ()
{
	std::shared_ptr<nano::transport::channel> channel;
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped && !(channel = scoring.channel ()))
	{
		condition.wait_for (lock, std::chrono::milliseconds{ config.bootstrap_ascending.throttle_wait }, [this] () { return stopped; });
	}
	return channel;
}

nano::account nano::bootstrap_ascending::service::next_priority ()
{
	debug_assert (!mutex.try_lock ());

	auto account = accounts.next_priority ();
	if (!account.is_zero ())
	{
		return account;
	}
	return { 0 };
}

nano::account nano::bootstrap_ascending::service::wait_priority ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = next_priority ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending_next, nano::stat::detail::next_priority);
			accounts.timestamp_set (account);
			return account;
		}
		else
		{
			condition.wait_for (lock, 100ms);
		}
	}
	return { 0 };
}

nano::account nano::bootstrap_ascending::service::next_database (bool should_throttle)
{
	debug_assert (!mutex.try_lock ());

	// Throttling increases the weight of database requests
	// TODO: Make this ratio configurable
	if (database_limiter.should_pass (should_throttle ? 22 : 1))
	{
		auto account = iterator.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending_next, nano::stat::detail::next_database);
			return account;
		}
	}
	return { 0 };
}

nano::account nano::bootstrap_ascending::service::wait_database (bool should_throttle)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = next_database (should_throttle);
		if (!account.is_zero ())
		{
			return account;
		}
		else
		{
			condition.wait_for (lock, 100ms);
		}
	}
	return { 0 };
}

nano::block_hash nano::bootstrap_ascending::service::next_dependency ()
{
	debug_assert (!mutex.try_lock ());

	auto dependency = accounts.next_blocking ();
	if (!dependency.is_zero ())
	{
		return dependency;
	}
	return { 0 };
}

nano::block_hash nano::bootstrap_ascending::service::wait_dependency ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto dependency = next_dependency ();
		if (!dependency.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending_next, nano::stat::detail::next_dependency);
			return dependency;
		}
		else
		{
			condition.wait_for (lock, 100ms);
		}
	}
	return { 0 };
}

bool nano::bootstrap_ascending::service::request (nano::account account, std::shared_ptr<nano::transport::channel> const & channel)
{
	async_tag tag{};
	tag.account = account;

	// Check if the account picked has blocks, if it does, start the pull from the highest block
	auto info = ledger.store.account.get (ledger.store.tx_begin_read (), account);
	if (info)
	{
		tag.type = query_type::blocks_by_hash;
		tag.start = info->head;
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

bool nano::bootstrap_ascending::service::request_info (nano::block_hash hash, std::shared_ptr<nano::transport::channel> const & channel)
{
	async_tag tag{};
	tag.type = query_type::account_info_by_hash;
	tag.start = hash;

	on_request.notify (tag, channel);

	send (channel, tag);

	return true; // Request sent
}

void nano::bootstrap_ascending::service::run_one_priority ()
{
	wait_blockprocessor ();
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto account = wait_priority ();
	if (account.is_zero ())
	{
		return;
	}
	request (account, channel);
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
	request (account, channel);
}

void nano::bootstrap_ascending::service::run_database ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// Avoid high churn rate of database requests
		bool should_throttle = !iterator.warmup () && throttle.throttled ();
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop_database);
		run_one_database (should_throttle);
		lock.lock ();
	}
}

void nano::bootstrap_ascending::service::run_one_dependency ()
{
	wait_blockprocessor ();
	auto channel = wait_channel ();
	if (!channel)
	{
		return;
	}
	auto dependency = wait_dependency ();
	if (dependency.is_zero ())
	{
		return;
	}
	request_info (dependency, channel);
}

void nano::bootstrap_ascending::service::run_dependencies ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop_dependencies);
		run_one_dependency ();
		lock.lock ();
	}
}

void nano::bootstrap_ascending::service::run_timeouts ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		scoring.sync (network.list ());
		scoring.timeout ();
		throttle.resize (compute_throttle_size ());

		auto const cutoff = std::chrono::steady_clock::now () - config.bootstrap_ascending.request_timeout;
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

		condition.wait_for (lock, 1s, [this] () { return stopped; });
	}
}

void nano::bootstrap_ascending::service::process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// Only process messages that have a known tag
	auto & tags_by_id = tags.get<tag_id> ();
	if (tags_by_id.count (message.id) > 0)
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::reply);

		auto iterator = tags_by_id.find (message.id);
		auto tag = *iterator;
		tags_by_id.erase (iterator);

		stats.inc (nano::stat::type::bootstrap_ascending_reply, to_stat_detail (tag.type));

		// Track bootstrap request response time
		stats.sample (nano::stat::sample::bootstrap_tag_duration, nano::log::milliseconds_delta (tag.timestamp), { 0, config.bootstrap_ascending.request_timeout.count () });

		scoring.received_message (channel);

		lock.unlock ();

		on_reply.notify (tag);
		condition.notify_all ();

		std::visit ([this, &tag] (auto && request) { return process (request, tag); }, message.payload);
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::missing_tag);
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::blocks_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
	stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::blocks);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.inc (nano::stat::type::bootstrap_ascending_verify, nano::stat::detail::ok);
			stats.add (nano::stat::type::bootstrap_ascending, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

			for (auto & block : response.blocks)
			{
				block_processor.add (block, nano::block_source::bootstrap);
			}

			nano::lock_guard<nano::mutex> lock{ mutex };
			throttle.add (true);
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_ascending_verify, nano::stat::detail::nothing_new);

			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.priority_down (tag.account);
			throttle.add (false);
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (nano::stat::type::bootstrap_ascending_verify, nano::stat::detail::invalid);
			// TODO: Log
		}
		break;
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::account_info_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
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
			accounts.priority_up (response.account);
		}
	}
}

void nano::bootstrap_ascending::service::process (const nano::asc_pull_ack::frontiers_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
{
	// TODO: Make use of frontiers info
	stats.inc (nano::stat::type::bootstrap_ascending_process, nano::stat::detail::frontiers);
}

void nano::bootstrap_ascending::service::process (const nano::empty_payload & response, const nano::bootstrap_ascending::service::async_tag & tag)
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
	// Scales logarithmically with ledger block
	// Returns: config.throttle_coefficient * sqrt(block_count)
	std::size_t size_new = config.bootstrap_ascending.throttle_coefficient * std::sqrt (ledger.block_count ());
	return size_new == 0 ? 16 : size_new;
}

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::service::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "tags", tags.size (), sizeof (decltype (tags)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle", throttle.size (), 0 }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle_successes", throttle.successes (), 0 }));
	composite->add_component (accounts.collect_container_info ("accounts"));
	return composite;
}

/*
 *
 */

nano::stat::detail nano::bootstrap_ascending::to_stat_detail (nano::bootstrap_ascending::service::query_type type)
{
	return nano::enum_util::cast<nano::stat::detail> (type);
}