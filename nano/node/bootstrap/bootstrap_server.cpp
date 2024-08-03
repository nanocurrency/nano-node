#include <nano/lib/blocks.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>

nano::bootstrap_server::bootstrap_server (bootstrap_server_config const & config_a, nano::store::component & store_a, nano::ledger & ledger_a, nano::network_constants const & network_constants_a, nano::stats & stats_a) :
	config{ config_a },
	store{ store_a },
	ledger{ ledger_a },
	network_constants{ network_constants_a },
	stats{ stats_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.max_queue;
	};

	queue.priority_query = [this] (auto const & origin) {
		return size_t{ 1 };
	};
}

nano::bootstrap_server::~bootstrap_server ()
{
	debug_assert (threads.empty ());
}

void nano::bootstrap_server::start ()
{
	debug_assert (threads.empty ());

	for (auto i = 0u; i < config.threads; ++i)
	{
		threads.push_back (std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::bootstrap_server);
			run ();
		}));
	}
}

void nano::bootstrap_server::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	for (auto & thread : threads)
	{
		thread.join ();
	}
	threads.clear ();
}

bool nano::bootstrap_server::verify_request_type (nano::asc_pull_type type) const
{
	switch (type)
	{
		case asc_pull_type::invalid:
			return false;
		case asc_pull_type::blocks:
		case asc_pull_type::account_info:
		case asc_pull_type::frontiers:
			return true;
	}
	return false;
}

bool nano::bootstrap_server::verify (const nano::asc_pull_req & message) const
{
	if (!verify_request_type (message.type))
	{
		return false;
	}

	struct verify_visitor
	{
		bool operator() (nano::empty_payload const &) const
		{
			return false;
		}
		bool operator() (nano::asc_pull_req::blocks_payload const & pld) const
		{
			return pld.count > 0 && pld.count <= max_blocks;
		}
		bool operator() (nano::asc_pull_req::account_info_payload const & pld) const
		{
			return !pld.target.is_zero ();
		}
		bool operator() (nano::asc_pull_req::frontiers_payload const & pld) const
		{
			return pld.count > 0 && pld.count <= max_frontiers;
		}
	};

	return std::visit (verify_visitor{}, message.payload);
}

bool nano::bootstrap_server::request (nano::asc_pull_req const & message, std::shared_ptr<nano::transport::channel> channel)
{
	if (!verify (message))
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::invalid);
		return false;
	}

	// If channel is full our response will be dropped anyway, so filter that early
	// TODO: Add per channel limits (this ideally should be done on the channel message processing side)
	if (channel->max (nano::transport::traffic_type::bootstrap))
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::channel_full, nano::stat::dir::in);
		return false;
	}

	bool added = false;
	{
		std::lock_guard guard{ mutex };
		added = queue.push ({ message, channel }, { nano::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::request);
		stats.inc (nano::stat::type::bootstrap_server_request, to_stat_detail (message.type));

		condition.notify_one ();
	}
	else
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::overfill);
		stats.inc (nano::stat::type::bootstrap_server_overfill, to_stat_detail (message.type));
	}
	return added;
}

void nano::bootstrap_server::respond (nano::asc_pull_ack & response, std::shared_ptr<nano::transport::channel> const & channel)
{
	stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response, nano::stat::dir::out);
	stats.inc (nano::stat::type::bootstrap_server_response, to_stat_detail (response.type));

	// Increase relevant stats depending on payload type
	struct stat_visitor
	{
		nano::stats & stats;

		void operator() (nano::empty_payload const &)
		{
			debug_assert (false, "missing payload");
		}
		void operator() (nano::asc_pull_ack::blocks_payload const & pld)
		{
			stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::blocks, nano::stat::dir::out, pld.blocks.size ());
		}
		void operator() (nano::asc_pull_ack::account_info_payload const & pld)
		{
		}
		void operator() (nano::asc_pull_ack::frontiers_payload const & pld)
		{
			stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::frontiers, nano::stat::dir::out, pld.frontiers.size ());
		}
	};
	std::visit (stat_visitor{ stats }, response.payload);

	on_response.notify (response, channel);

	channel->send (
	response, [this] (auto & ec, auto size) {
		if (ec)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::write_error, nano::stat::dir::out);
		}
	},
	nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type::bootstrap);
}

void nano::bootstrap_server::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (!queue.empty ())
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::loop);

			run_batch (lock);
			debug_assert (!lock.owns_lock ());

			lock.lock ();
		}
		else
		{
			condition.wait (lock, [this] () { return stopped || !queue.empty (); });
		}
	}
}

void nano::bootstrap_server::run_batch (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	debug_assert (config.batch_size > 0);
	auto batch = queue.next_batch (config.batch_size);

	lock.unlock ();

	auto transaction = ledger.tx_begin_read ();

	for (auto const & [value, origin] : batch)
	{
		auto const & [request, channel] = value;

		transaction.refresh_if_needed ();

		if (!channel->max (nano::transport::traffic_type::bootstrap))
		{
			auto response = process (transaction, request);
			respond (response, channel);
		}
		else
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::channel_full, nano::stat::dir::out);
		}
	}
}

nano::asc_pull_ack nano::bootstrap_server::process (secure::transaction const & transaction, nano::asc_pull_req const & message)
{
	return std::visit ([this, &transaction, &message] (auto && request) { return process (transaction, message.id, request); }, message.payload);
}

nano::asc_pull_ack nano::bootstrap_server::process (secure::transaction const &, nano::asc_pull_req::id_t id, nano::empty_payload const & request)
{
	// Empty payload should never be possible, but return empty response anyway
	debug_assert (false, "missing payload");
	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::invalid;
	return response;
}

/*
 * Blocks request
 */

nano::asc_pull_ack nano::bootstrap_server::process (secure::transaction const & transaction, nano::asc_pull_req::id_t id, nano::asc_pull_req::blocks_payload const & request) const
{
	const std::size_t count = std::min (static_cast<std::size_t> (request.count), max_blocks);

	switch (request.start_type)
	{
		case asc_pull_req::hash_type::block:
		{
			if (ledger.any.block_exists (transaction, request.start.as_block_hash ()))
			{
				return prepare_response (transaction, id, request.start.as_block_hash (), count);
			}
		}
		break;
		case asc_pull_req::hash_type::account:
		{
			auto info = ledger.any.account_get (transaction, request.start.as_account ());
			if (info)
			{
				// Start from open block if pulling by account
				return prepare_response (transaction, id, info->open_block, count);
			}
		}
		break;
	}

	// Neither block nor account found, send empty response to indicate that
	return prepare_empty_blocks_response (id);
}

nano::asc_pull_ack nano::bootstrap_server::prepare_response (secure::transaction const & transaction, nano::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count) const
{
	debug_assert (count <= max_blocks); // Should be filtered out earlier

	auto blocks = prepare_blocks (transaction, start_block, count);
	debug_assert (blocks.size () <= count);

	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::blocks;

	nano::asc_pull_ack::blocks_payload response_payload{};
	response_payload.blocks = blocks;
	response.payload = response_payload;

	response.update_header ();
	return response;
}

nano::asc_pull_ack nano::bootstrap_server::prepare_empty_blocks_response (nano::asc_pull_req::id_t id) const
{
	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::blocks;

	nano::asc_pull_ack::blocks_payload empty_payload{};
	response.payload = empty_payload;

	response.update_header ();
	return response;
}

std::vector<std::shared_ptr<nano::block>> nano::bootstrap_server::prepare_blocks (secure::transaction const & transaction, nano::block_hash start_block, std::size_t count) const
{
	debug_assert (count <= max_blocks); // Should be filtered out earlier

	std::vector<std::shared_ptr<nano::block>> result;
	if (!start_block.is_zero ())
	{
		std::shared_ptr<nano::block> current = ledger.any.block_get (transaction, start_block);
		while (current && result.size () < count)
		{
			result.push_back (current);

			auto successor = current->sideband ().successor;
			current = ledger.any.block_get (transaction, successor);
		}
	}
	return result;
}

/*
 * Account info request
 */

nano::asc_pull_ack nano::bootstrap_server::process (secure::transaction const & transaction, nano::asc_pull_req::id_t id, nano::asc_pull_req::account_info_payload const & request) const
{
	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::account_info;

	nano::account target{ 0 };
	switch (request.target_type)
	{
		case asc_pull_req::hash_type::account:
		{
			target = request.target.as_account ();
		}
		break;
		case asc_pull_req::hash_type::block:
		{
			// Try to lookup account assuming target is block hash
			target = ledger.any.block_account (transaction, request.target.as_block_hash ()).value_or (0);
		}
		break;
	}

	nano::asc_pull_ack::account_info_payload response_payload{};
	response_payload.account = target;

	auto account_info = ledger.any.account_get (transaction, target);
	if (account_info)
	{
		response_payload.account_open = account_info->open_block;
		response_payload.account_head = account_info->head;
		response_payload.account_block_count = account_info->block_count;

		auto conf_info = store.confirmation_height.get (transaction, target);
		if (conf_info)
		{
			response_payload.account_conf_frontier = conf_info->frontier;
			response_payload.account_conf_height = conf_info->height;
		}
	}
	// If account is missing the response payload will contain all 0 fields, except for the target

	response.payload = response_payload;
	response.update_header ();
	return response;
}

/*
 * Frontiers request
 */

nano::asc_pull_ack nano::bootstrap_server::process (secure::transaction const & transaction, nano::asc_pull_req::id_t id, nano::asc_pull_req::frontiers_payload const & request) const
{
	debug_assert (request.count <= max_frontiers); // Should be filtered out earlier

	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::frontiers;

	nano::asc_pull_ack::frontiers_payload response_payload{};
	for (auto it = store.account.begin (transaction, request.start), end = store.account.end (); it != end && response_payload.frontiers.size () < request.count; ++it)
	{
		response_payload.frontiers.emplace_back (it->first, it->second.head);
	}

	response.payload = response_payload;
	response.update_header ();
	return response;
}

/*
 *
 */

nano::stat::detail nano::to_stat_detail (nano::asc_pull_type type)
{
	switch (type)
	{
		case asc_pull_type::blocks:
			return nano::stat::detail::blocks;
		case asc_pull_type::account_info:
			return nano::stat::detail::account_info;
		case asc_pull_type::frontiers:
			return nano::stat::detail::frontiers;
		default:
			return nano::stat::detail::invalid;
	}
}

/*
 * bootstrap_server_config
 */

nano::error nano::bootstrap_server_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_queue", max_queue, "Maximum number of queued requests per peer. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads to process requests. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Maximum number of requests to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::bootstrap_server_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_queue", max_queue);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
