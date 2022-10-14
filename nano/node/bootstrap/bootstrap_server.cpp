#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/store.hpp>

#include <boost/asio/error.hpp>

nano::bootstrap_server::bootstrap_server (nano::store & store_a, nano::network_constants const & network_constants_a, nano::stat & stats_a) :
	store{ store_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	request_queue{ stats, nano::stat::type::bootstrap_server_requests, nano::thread_role::name::bootstrap_server_requests, /* threads */ 1, /* max size */ 1024 * 16, /* max batch */ 128 },
	response_queue{ stats, nano::stat::type::bootstrap_server_responses, nano::thread_role::name::bootstrap_server_responses, /* threads */ 1, /* max size */ 1024 * 16 /* max batch unlimited*/ }
{
	request_queue.process_batch = [this] (auto & batch) {
		process_batch (batch);
	};

	response_queue.process_batch = [this] (auto & batch) {
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
	response_queue.start ();
}

void nano::bootstrap_server::stop ()
{
	request_queue.stop ();
	response_queue.stop ();
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
		auto response = process (transaction, request);
		if (response)
		{
			stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::blocks, nano::stat::dir::out, response->blocks ().size ());

			response_queue.add (std::make_pair (std::move (response), channel));
		}
	}
}

std::unique_ptr<nano::asc_pull_ack> nano::bootstrap_server::process (nano::transaction & transaction, nano::asc_pull_req const & message)
{
	// `start` can represent either account or block hash
	if (store.block.exists (transaction, message.start.as_block_hash ()))
	{
		return prepare_response (transaction, message.id, message.start.as_block_hash (), max_blocks);
	}
	if (store.account.exists (transaction, message.start.as_account ()))
	{
		auto info = store.account.get (transaction, message.start.as_account ());
		if (info)
		{
			// Start from open block if pulling by account
			return prepare_response (transaction, message.id, info->open_block, max_blocks);
		}
	}

	// Neither block nor account found, send empty response to indicate that
	return prepare_empty_response (message.id);
}

std::unique_ptr<nano::asc_pull_ack> nano::bootstrap_server::prepare_response (nano::transaction & transaction, nano::asc_pull_req::id_t id, nano::block_hash start_block, std::size_t count)
{
	auto blocks = prepare_blocks (transaction, start_block, count);
	debug_assert (blocks.size () <= count);

	auto response = std::make_unique<nano::asc_pull_ack> (network_constants);
	response->id = id;
	response->blocks (blocks);
	return response;
}

std::unique_ptr<nano::asc_pull_ack> nano::bootstrap_server::prepare_empty_response (nano::asc_pull_req::id_t id)
{
	auto response = std::make_unique<nano::asc_pull_ack> (network_constants);
	response->id = id;
	return response;
}

std::vector<std::shared_ptr<nano::block>> nano::bootstrap_server::prepare_blocks (nano::transaction & transaction, nano::block_hash start_block, std::size_t count) const
{
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
 * Responses
 */

void nano::bootstrap_server::process_batch (std::deque<response_t> & batch)
{
	for (auto & [response, channel] : batch)
	{
		debug_assert (response != nullptr);

		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response, nano::stat::dir::out);
		on_response.notify (*response, channel);

		channel->send (
		*response, [this] (auto & ec, auto size) {
			if (ec)
			{
				stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::write_error, nano::stat::dir::out);
			}
		},
		nano::buffer_drop_policy::limiter, nano::bandwidth_limit_type::bootstrap);
	}
}