#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/store.hpp>

#include <boost/asio/error.hpp>

// TODO: Make threads configurable
nano::bootstrap_server::bootstrap_server (nano::store & store_a, nano::network_constants const & network_constants_a, nano::stat & stats_a) :
	store{ store_a },
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

bool nano::bootstrap_server::valid_request_type (nano::asc_pull_type type) const
{
	if (type == nano::asc_pull_type::blocks)
	{
		return true;
	}
	return false;
}

bool nano::bootstrap_server::request (nano::asc_pull_req const & message, std::shared_ptr<nano::transport::channel> channel)
{
	// Futureproofing
	if (!valid_request_type (message.type))
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::invalid_message_type);
		return false;
	}

	// If channel is full our response will be dropped anyway, so filter that early
	// TODO: Add per channel limits (this ideally should be done on the channel message processing side)
	if (channel->max ())
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::drop);
		return false;
	}

	if (message.count == 0 || message.count > max_blocks)
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::bad_count);
		return false;
	}

	request_queue.add (std::make_pair (message, channel));
	return true;
}

/*
 * Requests
 */

void nano::bootstrap_server::process_batch (std::deque<request_t> & batch)
{
	auto transaction = store.tx_begin_read ();

	for (auto & [request, channel] : batch)
	{
		if (!channel->max ())
		{
			auto response = process (transaction, request);
			respond (response, channel);
		}
		else
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::drop);
		}
	}
}

nano::asc_pull_ack nano::bootstrap_server::process (nano::transaction & transaction, nano::asc_pull_req const & message)
{
	const std::size_t count = std::min (static_cast<std::size_t> (message.count), max_blocks);

	// `start` can represent either account or block hash
	if (store.block.exists (transaction, message.start.as_block_hash ()))
	{
		return prepare_response (transaction, message.id, message.start.as_block_hash (), count);
	}
	if (store.account.exists (transaction, message.start.as_account ()))
	{
		auto info = store.account.get (transaction, message.start.as_account ());
		if (info)
		{
			// Start from open block if pulling by account
			return prepare_response (transaction, message.id, info->open_block, count);
		}
		else
		{
			debug_assert (false, "account exists but cannot be retrieved");
		}
	}

	// Neither block nor account found, send empty response to indicate that
	return prepare_empty_response (message.id);
}

nano::asc_pull_ack nano::bootstrap_server::prepare_response (nano::transaction & transaction, nano::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count)
{
	debug_assert (count <= max_blocks);

	auto blocks = prepare_blocks (transaction, start_block, count);
	debug_assert (blocks.size () <= count);

	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	response.blocks (blocks);
	return response;
}

nano::asc_pull_ack nano::bootstrap_server::prepare_empty_response (nano::asc_pull_req::id_t id)
{
	nano::asc_pull_ack response{ network_constants };
	response.id = id;
	return response;
}

std::vector<std::shared_ptr<nano::block>> nano::bootstrap_server::prepare_blocks (nano::transaction & transaction, nano::block_hash start_block, std::size_t count) const
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

void nano::bootstrap_server::respond (nano::asc_pull_ack & response, std::shared_ptr<nano::transport::channel> & channel)
{
	stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response, nano::stat::dir::out);
	stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::blocks, nano::stat::dir::out, response.blocks ().size ());

	on_response.notify (response, channel);

	channel->send (
	response, [this] (auto & ec, auto size) {
		if (ec)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::write_error, nano::stat::dir::out);
		}
	},
	nano::buffer_drop_policy::limiter, nano::bandwidth_limit_type::bootstrap);
}