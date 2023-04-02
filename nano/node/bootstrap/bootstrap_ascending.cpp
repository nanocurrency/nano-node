#include <nano/lib/stats_enums.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

/*
 * database_iterator
 */

nano::bootstrap_ascending::database_iterator::database_iterator (nano::store & store_a, table_type table_a) :
	store{ store_a },
	table{ table_a }
{
}

nano::account nano::bootstrap_ascending::database_iterator::operator* () const
{
	return current;
}

void nano::bootstrap_ascending::database_iterator::next (nano::transaction & tx)
{
	switch (table)
	{
		case table_type::account:
		{
			auto i = current.number () + 1;
			auto item = store.account.begin (tx, i);
			if (item != store.account.end ())
			{
				current = item->first;
			}
			else
			{
				current = { 0 };
			}
			break;
		}
		case table_type::pending:
		{
			auto i = current.number () + 1;
			auto item = store.pending.begin (tx, nano::pending_key{ i, 0 });
			if (item != store.pending.end ())
			{
				current = item->first.account;
			}
			else
			{
				current = { 0 };
			}
			break;
		}
	}
}

/*
 * buffered_iterator
 */

nano::bootstrap_ascending::buffered_iterator::buffered_iterator (nano::store & store_a) :
	store{ store_a },
	accounts_iterator{ store, database_iterator::table_type::account },
	pending_iterator{ store, database_iterator::table_type::pending }
{
}

nano::account nano::bootstrap_ascending::buffered_iterator::operator* () const
{
	return !buffer.empty () ? buffer.front () : nano::account{ 0 };
}

nano::account nano::bootstrap_ascending::buffered_iterator::next ()
{
	if (!buffer.empty ())
	{
		buffer.pop_front ();
	}
	else
	{
		fill ();
	}

	return *(*this);
}

void nano::bootstrap_ascending::buffered_iterator::fill ()
{
	debug_assert (buffer.empty ());

	// Fill half from accounts table and half from pending table
	auto transaction = store.tx_begin_read ();

	for (int n = 0; n < size / 2; ++n)
	{
		accounts_iterator.next (transaction);
		if (!(*accounts_iterator).is_zero ())
		{
			buffer.push_back (*accounts_iterator);
		}
	}

	for (int n = 0; n < size / 2; ++n)
	{
		pending_iterator.next (transaction);
		if (!(*pending_iterator).is_zero ())
		{
			buffer.push_back (*pending_iterator);
		}
	}
}

/*
 * account_sets
 */

nano::bootstrap_ascending::account_sets::account_sets (nano::stats & stats_a, nano::account_sets_config config_a) :
	stats{ stats_a },
	config{ std::move (config_a) }
{
}

void nano::bootstrap_ascending::account_sets::priority_up (nano::account const & account)
{
	if (!blocked (account))
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::prioritize);

		auto iter = priorities.get<tag_account> ().find (account);
		if (iter != priorities.get<tag_account> ().end ())
		{
			priorities.get<tag_account> ().modify (iter, [] (auto & val) {
				val.priority = std::min ((val.priority * account_sets::priority_increase), account_sets::priority_max);
			});
		}
		else
		{
			priorities.get<tag_account> ().insert ({ account, account_sets::priority_initial });
			stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::priority_insert);

			trim_overflow ();
		}
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::prioritize_failed);
	}
}

void nano::bootstrap_ascending::account_sets::priority_down (nano::account const & account)
{
	auto iter = priorities.get<tag_account> ().find (account);
	if (iter != priorities.get<tag_account> ().end ())
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::deprioritize);

		auto priority_new = iter->priority - account_sets::priority_decrease;
		if (priority_new <= account_sets::priority_cutoff)
		{
			priorities.get<tag_account> ().erase (iter);
			stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::priority_erase_threshold);
		}
		else
		{
			priorities.get<tag_account> ().modify (iter, [priority_new] (auto & val) {
				val.priority = priority_new;
			});
		}
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::deprioritize_failed);
	}
}

void nano::bootstrap_ascending::account_sets::block (nano::account const & account, nano::block_hash const & dependency)
{
	stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::block);

	auto existing = priorities.get<tag_account> ().find (account);
	auto entry = existing == priorities.get<tag_account> ().end () ? priority_entry{ 0, 0 } : *existing;

	priorities.get<tag_account> ().erase (account);
	stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::priority_erase_block);

	blocking.get<tag_account> ().insert ({ account, dependency, entry });
	stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::blocking_insert);

	trim_overflow ();
}

void nano::bootstrap_ascending::account_sets::unblock (nano::account const & account, std::optional<nano::block_hash> const & hash)
{
	// Unblock only if the dependency is fulfilled
	auto existing = blocking.get<tag_account> ().find (account);
	if (existing != blocking.get<tag_account> ().end () && (!hash || existing->dependency == *hash))
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::unblock);

		debug_assert (priorities.get<tag_account> ().count (account) == 0);
		if (!existing->original_entry.account.is_zero ())
		{
			debug_assert (existing->original_entry.account == account);
			priorities.get<tag_account> ().insert (existing->original_entry);
		}
		else
		{
			priorities.get<tag_account> ().insert ({ account, account_sets::priority_initial });
		}
		blocking.get<tag_account> ().erase (account);

		trim_overflow ();
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::unblock_failed);
	}
}

void nano::bootstrap_ascending::account_sets::timestamp (const nano::account & account, bool reset)
{
	const nano::millis_t tstamp = reset ? 0 : nano::milliseconds_since_epoch ();

	auto iter = priorities.get<tag_account> ().find (account);
	if (iter != priorities.get<tag_account> ().end ())
	{
		priorities.get<tag_account> ().modify (iter, [tstamp] (auto & entry) {
			entry.timestamp = tstamp;
		});
	}
}

bool nano::bootstrap_ascending::account_sets::check_timestamp (const nano::account & account) const
{
	auto iter = priorities.get<tag_account> ().find (account);
	if (iter != priorities.get<tag_account> ().end ())
	{
		if (nano::milliseconds_since_epoch () - iter->timestamp < config.cooldown)
		{
			return false;
		}
	}
	return true;
}

void nano::bootstrap_ascending::account_sets::trim_overflow ()
{
	if (priorities.size () > config.priorities_max)
	{
		// Evict the lowest priority entry
		priorities.get<tag_priority> ().erase (priorities.get<tag_priority> ().begin ());

		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::priority_erase_overflow);
	}
	if (blocking.size () > config.blocking_max)
	{
		// Evict the lowest priority entry
		blocking.get<tag_priority> ().erase (blocking.get<tag_priority> ().begin ());

		stats.inc (nano::stat::type::bootstrap_ascending_accounts, nano::stat::detail::blocking_erase_overflow);
	}
}

nano::account nano::bootstrap_ascending::account_sets::next ()
{
	if (priorities.empty ())
	{
		return { 0 };
	}

	std::vector<float> weights;
	std::vector<nano::account> candidates;

	int iterations = 0;
	while (candidates.size () < config.consideration_count && iterations++ < config.consideration_count * 10)
	{
		debug_assert (candidates.size () == weights.size ());

		// Use a dedicated, uniformly distributed field for sampling to avoid problematic corner case when accounts in the queue are very close together
		auto search = bootstrap_ascending::generate_id ();
		auto iter = priorities.get<tag_id> ().lower_bound (search);
		if (iter == priorities.get<tag_id> ().end ())
		{
			iter = priorities.get<tag_id> ().begin ();
		}

		if (check_timestamp (iter->account))
		{
			candidates.push_back (iter->account);
			weights.push_back (iter->priority);
		}
	}

	if (candidates.empty ())
	{
		return { 0 }; // All sampled accounts are busy
	}

	std::discrete_distribution dist{ weights.begin (), weights.end () };
	auto selection = dist (rng);
	debug_assert (!weights.empty () && selection < weights.size ());
	auto result = candidates[selection];
	return result;
}

bool nano::bootstrap_ascending::account_sets::blocked (nano::account const & account) const
{
	return blocking.get<tag_account> ().count (account) > 0;
}

std::size_t nano::bootstrap_ascending::account_sets::priority_size () const
{
	return priorities.size ();
}

std::size_t nano::bootstrap_ascending::account_sets::blocked_size () const
{
	return blocking.size ();
}

float nano::bootstrap_ascending::account_sets::priority (nano::account const & account) const
{
	if (blocked (account))
	{
		return 0.0f;
	}
	auto existing = priorities.get<tag_account> ().find (account);
	if (existing != priorities.get<tag_account> ().end ())
	{
		return existing->priority;
	}
	return account_sets::priority_cutoff;
}

auto nano::bootstrap_ascending::account_sets::info () const -> info_t
{
	return { blocking, priorities };
}

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::account_sets::collect_container_info (const std::string & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "priorities", priorities.size (), sizeof (decltype (priorities)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocking", blocking.size (), sizeof (decltype (blocking)::value_type) }));
	return composite;
}

/*
 * priority_entry
 */

nano::bootstrap_ascending::account_sets::priority_entry::priority_entry (nano::account account_a, float priority_a) :
	account{ account_a },
	priority{ priority_a }
{
	id = nano::bootstrap_ascending::generate_id ();
}

/*
 * bootstrap_ascending
 */

nano::bootstrap_ascending::bootstrap_ascending (nano::node & node_a, nano::store & store_a, nano::block_processor & block_processor_a, nano::ledger & ledger_a, nano::network & network_a, nano::stats & stat_a) :
	node{ node_a },
	store{ store_a },
	block_processor{ block_processor_a },
	ledger{ ledger_a },
	network{ network_a },
	stats{ stat_a },
	accounts{ stats },
	iterator{ store },
	limiter{ node.config.bootstrap_ascending.requests_limit, 1.0 },
	database_limiter{ node.config.bootstrap_ascending.database_requests_limit, 1.0 }
{
	// TODO: This is called from a very congested blockprocessor thread. Offload this work to a dedicated processing thread
	block_processor.batch_processed.add ([this] (auto const & batch) {
		{
			nano::lock_guard<nano::mutex> lock{ mutex };

			auto transaction = store.tx_begin_read ();
			for (auto const & [result, block] : batch)
			{
				debug_assert (block != nullptr);

				inspect (transaction, result, *block);
			}
		}

		condition.notify_all ();
	});
}

nano::bootstrap_ascending::~bootstrap_ascending ()
{
	// All threads must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (!timeout_thread.joinable ());
}

void nano::bootstrap_ascending::start ()
{
	debug_assert (!thread.joinable ());
	debug_assert (!timeout_thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run ();
	});

	timeout_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascending_bootstrap);
		run_timeouts ();
	});
}

void nano::bootstrap_ascending::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	lock.unlock ();
	condition.notify_all ();
	nano::join_or_pass (thread);
	nano::join_or_pass (timeout_thread);
}

nano::bootstrap_ascending::id_t nano::bootstrap_ascending::generate_id ()
{
	id_t id;
	nano::random_pool::generate_block (reinterpret_cast<uint8_t *> (&id), sizeof (id));
	return id;
}

void nano::bootstrap_ascending::send (std::shared_ptr<nano::transport::channel> channel, async_tag tag)
{
	debug_assert (tag.type == async_tag::query_type::blocks_by_hash || tag.type == async_tag::query_type::blocks_by_account);

	nano::asc_pull_req request{ node.network_params.network };
	request.id = tag.id;
	request.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload request_payload;
	request_payload.start = tag.start;
	request_payload.count = node.config.bootstrap_ascending.pull_count;
	request_payload.start_type = (tag.type == async_tag::query_type::blocks_by_hash) ? nano::asc_pull_req::hash_type::block : nano::asc_pull_req::hash_type::account;

	request.payload = request_payload;
	request.update_header ();

	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::request, nano::stat::dir::out);

	// TODO: There is no feedback mechanism if bandwidth limiter starts dropping our requests
	channel->send (
	request, nullptr,
	nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type::bootstrap);
}

size_t nano::bootstrap_ascending::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.priority_size ();
}

size_t nano::bootstrap_ascending::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap_ascending::inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block)
{
	auto const hash = block.hash ();

	switch (result.code)
	{
		case nano::process_result::progress:
		{
			const auto account = ledger.account (tx, hash);
			const auto is_send = ledger.is_send (tx, block);

			// If we've inserted any block in to an account, unmark it as blocked
			accounts.unblock (account);
			accounts.priority_up (account);
			accounts.timestamp (account, /* reset timestamp */ true);

			if (is_send)
			{
				// TODO: Encapsulate this as a helper somewhere
				nano::account destination{ 0 };
				switch (block.type ())
				{
					case nano::block_type::send:
						destination = block.destination ();
						break;
					case nano::block_type::state:
						destination = block.link ().as_account ();
						break;
					default:
						debug_assert (false, "unexpected block type");
						break;
				}
				if (!destination.is_zero ())
				{
					accounts.unblock (destination, hash); // Unblocking automatically inserts account into priority set
					accounts.priority_up (destination);
				}
			}
		}
		break;
		case nano::process_result::gap_source:
		{
			const auto account = block.previous ().is_zero () ? block.account () : ledger.account (tx, block.previous ());
			const auto source = block.source ().is_zero () ? block.link ().as_block_hash () : block.source ();

			// Mark account as blocked because it is missing the source block
			accounts.block (account, source);

			// TODO: Track stats
		}
		break;
		case nano::process_result::old:
		{
			// TODO: Track stats
		}
		break;
		case nano::process_result::gap_previous:
		{
			// TODO: Track stats
		}
		break;
		default: // No need to handle other cases
			break;
	}
}

void nano::bootstrap_ascending::wait_blockprocessor ()
{
	while (!stopped && block_processor.half_full ())
	{
		std::this_thread::sleep_for (500ms); // Blockprocessor is relatively slow, sleeping here instead of using conditions
	}
}

void nano::bootstrap_ascending::wait_available_request ()
{
	while (!stopped && !limiter.should_pass (1))
	{
		std::this_thread::sleep_for (50ms); // Give it at least some time to cooldown to avoid hitting the limit too frequently
	}
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::available_channel ()
{
	auto channels = network.random_set (32, node.network_params.network.bootstrap_protocol_version_min, /* include temporary channels */ true);
	for (auto & channel : channels)
	{
		if (!channel->max (nano::transport::traffic_type::bootstrap))
		{
			return channel;
		}
	}
	return nullptr;
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::wait_available_channel ()
{
	std::shared_ptr<nano::transport::channel> channel;
	while (!stopped && !(channel = available_channel ()))
	{
		std::this_thread::sleep_for (100ms);
	}
	return channel;
}

nano::account nano::bootstrap_ascending::available_account ()
{
	{
		auto account = accounts.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::next_priority);
			return account;
		}
	}

	if (database_limiter.should_pass (1))
	{
		auto account = iterator.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::next_database);
			return account;
		}
	}

	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::next_none);
	return { 0 };
}

nano::account nano::bootstrap_ascending::wait_available_account ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = available_account ();
		if (!account.is_zero ())
		{
			accounts.timestamp (account);
			return account;
		}
		else
		{
			condition.wait_for (lock, 100ms);
		}
	}
	return { 0 };
}

bool nano::bootstrap_ascending::request (nano::account & account, std::shared_ptr<nano::transport::channel> & channel)
{
	async_tag tag{};
	tag.id = generate_id ();
	tag.account = account;
	tag.time = nano::milliseconds_since_epoch ();

	// Check if the account picked has blocks, if it does, start the pull from the highest block
	auto info = store.account.get (store.tx_begin_read (), account);
	if (info)
	{
		tag.type = async_tag::query_type::blocks_by_hash;
		tag.start = info->head;
	}
	else
	{
		tag.type = async_tag::query_type::blocks_by_account;
		tag.start = account;
	}

	on_request.notify (tag, channel);

	track (tag);
	send (channel, tag);

	return true; // Request sent
}

bool nano::bootstrap_ascending::run_one ()
{
	// Ensure there is enough space in blockprocessor for queuing new blocks
	wait_blockprocessor ();

	// Do not do too many requests in parallel, impose throttling
	wait_available_request ();

	// Waits for channel that is not full
	auto channel = wait_available_channel ();
	if (!channel)
	{
		return false;
	}

	// Waits for account either from priority queue or database
	auto account = wait_available_account ();
	if (account.is_zero ())
	{
		return false;
	}

	bool success = request (account, channel);
	return success;
}

void nano::bootstrap_ascending::run ()
{
	while (!stopped)
	{
		stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::loop);
		run_one ();
	}
}

void nano::bootstrap_ascending::run_timeouts ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto & tags_by_order = tags.get<tag_sequenced> ();
		while (!tags_by_order.empty () && nano::time_difference (tags_by_order.front ().time, nano::milliseconds_since_epoch ()) > node.config.bootstrap_ascending.timeout)
		{
			auto tag = tags_by_order.front ();
			tags_by_order.pop_front ();
			on_timeout.notify (tag);
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::timeout);
		}
		condition.wait_for (lock, 1s, [this] () { return stopped; });
	}
}

void nano::bootstrap_ascending::process (const nano::asc_pull_ack & message)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// Only process messages that have a known tag
	auto & tags_by_id = tags.get<tag_id> ();
	if (tags_by_id.count (message.id) > 0)
	{
		auto iterator = tags_by_id.find (message.id);
		auto tag = *iterator;
		tags_by_id.erase (iterator);

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

void nano::bootstrap_ascending::process (const nano::asc_pull_ack::blocks_payload & response, const nano::bootstrap_ascending::async_tag & tag)
{
	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::reply);

	auto result = verify (response, tag);
	switch (result)
	{
		case verify_result::ok:
		{
			stats.add (nano::stat::type::bootstrap_ascending, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

			for (auto & block : response.blocks)
			{
				block_processor.add (block);
			}
		}
		break;
		case verify_result::nothing_new:
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::nothing_new);

			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				accounts.priority_down (tag.account);
			}
		}
		break;
		case verify_result::invalid:
		{
			stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::invalid);
			// TODO: Log
		}
		break;
	}
}

void nano::bootstrap_ascending::process (const nano::asc_pull_ack::account_info_payload & response, const nano::bootstrap_ascending::async_tag & tag)
{
	// TODO: Make use of account info
}

void nano::bootstrap_ascending::process (const nano::empty_payload & response, const nano::bootstrap_ascending::async_tag & tag)
{
	// Should not happen
	debug_assert (false, "empty payload");
}

nano::bootstrap_ascending::verify_result nano::bootstrap_ascending::verify (const nano::asc_pull_ack::blocks_payload & response, const nano::bootstrap_ascending::async_tag & tag) const
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
		case async_tag::query_type::blocks_by_hash:
		{
			if (first->hash () != tag.start.as_block_hash ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		case async_tag::query_type::blocks_by_account:
		{
			// Open & state blocks always contain account field
			if (first->account () != tag.start.as_account ())
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

void nano::bootstrap_ascending::track (async_tag const & tag)
{
	stats.inc (nano::stat::type::bootstrap_ascending, nano::stat::detail::track);

	nano::lock_guard<nano::mutex> lock{ mutex };
	debug_assert (tags.get<tag_id> ().count (tag.id) == 0);
	tags.get<tag_id> ().insert (tag);
}

auto nano::bootstrap_ascending::info () const -> account_sets::info_t
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.info ();
}

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "tags", tags.size (), sizeof (decltype (tags)::value_type) }));
	composite->add_component (accounts.collect_container_info ("accounts"));
	return composite;
}
