#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

// TODO: Make threads configurable
nano::bootstrap_server::bootstrap_server (nano::store & store_a, nano::ledger & ledger_a, nano::network_constants const & network_constants_a, nano::stats & stats_a) :
	store{ store_a },
	ledger{ ledger_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	request_queue{ stats, nano::stat::type::bootstrap_server, nano::thread_role::name::bootstrap_server, /* threads */ 1, /* max size */ 1024 * 16, /* max batch */ 128 }
{
	request_queue.process_batch = [this] (auto & batch) {
		process_batch (batch);
	};
}

nano::bootstrap_server::~bootstrap_server ()
{
	stop ();
}

void nano::bootstrap_server::start ()
{
	request_queue.start ();
}

void nano::bootstrap_server::stop ()
{
	request_queue.stop ();
}

bool nano::bootstrap_server::verify_request_type (nano::asc_pull_type type) const
{
	switch (type)
	{
		case asc_pull_type::invalid:
			return false;
		case asc_pull_type::blocks:
		case asc_pull_type::account_info:
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

	request_queue.add (std::make_pair (message, channel));
	return true;
}

void nano::bootstrap_server::respond (nano::asc_pull_ack & response, std::shared_ptr<nano::transport::channel> & channel)
{
	stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response, nano::stat::dir::out);

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
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response_blocks, nano::stat::dir::out);
			stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::blocks, nano::stat::dir::out, pld.blocks.size ());
		}
		void operator() (nano::asc_pull_ack::account_info_payload const & pld)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response_account_info, nano::stat::dir::out);
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

/*
 * Requests
 */

void nano::bootstrap_server::process_batch (std::deque<request_t> & batch)
{
	auto transaction = store.tx_begin_read ();

	for (auto & [request, channel] : batch)
	{
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

nano::asc_pull_ack nano::bootstrap_server::process (nano::transaction const & transaction, const nano::asc_pull_req & message)
{
	return std::visit ([this, &transaction, &message] (auto && request) { return process (transaction, message.id, request); }, message.payload);
}

nano::asc_pull_ack nano::bootstrap_server::process (const nano::transaction &, nano::asc_pull_req::id_t id, const nano::empty_payload & request)
{
	// Empty payload should never be possible, but return empty response anyway
	debug_assert (false, "missing payload");
	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::invalid;
	return response;
}

/*
 * Blocks response
 */

nano::asc_pull_ack nano::bootstrap_server::process (nano::transaction const & transaction, nano::asc_pull_req::id_t id, nano::asc_pull_req::blocks_payload const & request)
{
	const std::size_t count = std::min (static_cast<std::size_t> (request.count), max_blocks);

	switch (request.start_type)
	{
		case asc_pull_req::hash_type::block:
		{
			if (store.block.exists (transaction, request.start.as_block_hash ()))
			{
				return prepare_response (transaction, id, request.start.as_block_hash (), count);
			}
		}
		break;
		case asc_pull_req::hash_type::account:
		{
			auto info = ledger.account_info (transaction, request.start.as_account ());
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

nano::asc_pull_ack nano::bootstrap_server::prepare_response (nano::transaction const & transaction, nano::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count)
{
	debug_assert (count <= max_blocks);

	auto blocks = prepare_blocks (transaction, start_block, count);
	debug_assert (blocks.size () <= count);

	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::blocks;

	nano::asc_pull_ack::blocks_payload response_payload;
	response_payload.blocks = blocks;
	response.payload = response_payload;

	response.update_header ();
	return response;
}

nano::asc_pull_ack nano::bootstrap_server::prepare_empty_blocks_response (nano::asc_pull_req::id_t id)
{
	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.type = nano::asc_pull_type::blocks;

	nano::asc_pull_ack::blocks_payload empty_payload{};
	response.payload = empty_payload;

	response.update_header ();
	return response;
}

std::vector<std::shared_ptr<nano::block>> nano::bootstrap_server::prepare_blocks (nano::transaction const & transaction, nano::block_hash start_block, std::size_t count) const
{
	debug_assert (count <= max_blocks);

	std::vector<std::shared_ptr<nano::block>> result;
	if (!start_block.is_zero ())
	{
		std::shared_ptr<nano::block> current = store.block.get (transaction, start_block);
		while (current && result.size () < count)
		{
			result.push_back (current);

			auto successor = current->sideband ().successor;
			current = store.block.get (transaction, successor);
		}
	}
	return result;
}

/*
 * Account info response
 */

nano::asc_pull_ack nano::bootstrap_server::process (const nano::transaction & transaction, nano::asc_pull_req::id_t id, const nano::asc_pull_req::account_info_payload & request)
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
			target = ledger.account_safe (transaction, request.target.as_block_hash ());
		}
		break;
	}

	nano::asc_pull_ack::account_info_payload response_payload{};
	response_payload.account = target;

	auto account_info = ledger.account_info (transaction, target);
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
