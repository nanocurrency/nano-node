#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_bulk_push.hpp>
#include <nano/node/bootstrap/bootstrap_legacy.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>

#include <boost/format.hpp>

nano::bulk_push_client::bulk_push_client (std::shared_ptr<nano::bootstrap_client> const & connection_a, std::shared_ptr<nano::bootstrap_attempt_legacy> const & attempt_a) :
	connection (connection_a),
	attempt (attempt_a)
{
}

nano::bulk_push_client::~bulk_push_client ()
{
}

void nano::bulk_push_client::start ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	nano::bulk_push message{ node->network_params.network };
	auto this_l (shared_from_this ());
	connection->channel->send (
	message, [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->connection->node.lock ();
		if (!node)
		{
			return;
		}
		if (!ec)
		{
			this_l->push ();
		}
		else
		{
			node->nlogger.debug (nano::log::type::bulk_push_client, "Unable to send bulk push request: {}", ec.message ());
		}
	},
	nano::transport::buffer_drop_policy::no_limiter_drop);
}

void nano::bulk_push_client::push ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	std::shared_ptr<nano::block> block;
	bool finished (false);
	while (block == nullptr && !finished)
	{
		if (current_target.first.is_zero () || current_target.first == current_target.second)
		{
			finished = attempt->request_bulk_push_target (current_target);
		}
		if (!finished)
		{
			block = node->block (current_target.first);
			if (block == nullptr)
			{
				current_target.first = nano::block_hash (0);
			}
			else
			{
				node->nlogger.debug (nano::log::type::bulk_push_client, "Bulk pushing range: [{}:{}]", current_target.first.to_string (), current_target.second.to_string ());
			}
		}
	}
	if (finished)
	{
		send_finished ();
	}
	else
	{
		current_target.first = block->previous ();
		push_block (*block);
	}
}

void nano::bulk_push_client::send_finished ()
{
	nano::shared_const_buffer buffer (static_cast<uint8_t> (nano::block_type::not_a_block));
	auto this_l (shared_from_this ());
	connection->channel->send_buffer (buffer, [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		try
		{
			this_l->promise.set_value (false);
		}
		catch (std::future_error &)
		{
		}
	});
}

void nano::bulk_push_client::push_block (nano::block const & block_a)
{
	std::vector<uint8_t> buffer;
	{
		nano::vectorstream stream (buffer);
		nano::serialize_block (stream, block_a);
	}
	auto this_l (shared_from_this ());
	connection->channel->send_buffer (nano::shared_const_buffer (std::move (buffer)), [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
		auto node = this_l->connection->node.lock ();
		if (!node)
		{
			return;
		}
		if (!ec)
		{
			this_l->push ();
		}
		else
		{
			node->nlogger.debug (nano::log::type::bulk_push_client, "Error sending block during bulk push: {}", ec.message ());
		}
	});
}

nano::bulk_push_server::bulk_push_server (std::shared_ptr<nano::transport::tcp_server> const & connection_a) :
	receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
	connection (connection_a)
{
	receive_buffer->resize (256);
}

void nano::bulk_push_server::throttled_receive ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!node->block_processor.half_full ())
	{
		receive ();
	}
	else
	{
		auto this_l (shared_from_this ());
		node->workers.add_timed_task (std::chrono::steady_clock::now () + std::chrono::seconds (1), [this_l] () {
			if (!this_l->connection->stopped)
			{
				this_l->throttled_receive ();
			}
		});
	}
}

void nano::bulk_push_server::receive ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (node->bootstrap_initiator.in_progress ())
	{
		node->nlogger.debug (nano::log::type::bulk_push_server, "Aborting bulk push because a bootstrap attempt is in progress");
	}
	else
	{
		auto this_l (shared_from_this ());
		connection->socket->async_read (receive_buffer, 1, [this_l] (boost::system::error_code const & ec, std::size_t size_a) {
			auto node = this_l->connection->node.lock ();
			if (!node)
			{
				return;
			}
			if (!ec)
			{
				this_l->received_type ();
			}
			else
			{
				node->nlogger.debug (nano::log::type::bulk_push_server, "Error receiving block type: {}", ec.message ());
			}
		});
	}
}

void nano::bulk_push_server::received_type ()
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	auto this_l (shared_from_this ());
	nano::block_type type (static_cast<nano::block_type> (receive_buffer->data ()[0]));
	switch (type)
	{
		case nano::block_type::send:
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::send, nano::stat::dir::in);
			connection->socket->async_read (receive_buffer, nano::send_block::size, [this_l, type] (boost::system::error_code const & ec, std::size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case nano::block_type::receive:
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::receive, nano::stat::dir::in);
			connection->socket->async_read (receive_buffer, nano::receive_block::size, [this_l, type] (boost::system::error_code const & ec, std::size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case nano::block_type::open:
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::open, nano::stat::dir::in);
			connection->socket->async_read (receive_buffer, nano::open_block::size, [this_l, type] (boost::system::error_code const & ec, std::size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case nano::block_type::change:
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::change, nano::stat::dir::in);
			connection->socket->async_read (receive_buffer, nano::change_block::size, [this_l, type] (boost::system::error_code const & ec, std::size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case nano::block_type::state:
		{
			node->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::state_block, nano::stat::dir::in);
			connection->socket->async_read (receive_buffer, nano::state_block::size, [this_l, type] (boost::system::error_code const & ec, std::size_t size_a) {
				this_l->received_block (ec, size_a, type);
			});
			break;
		}
		case nano::block_type::not_a_block:
		{
			connection->start ();
			break;
		}
		default:
		{
			node->nlogger.debug (nano::log::type::bulk_push_server, "Unknown type received as block type");
			break;
		}
	}
}

void nano::bulk_push_server::received_block (boost::system::error_code const & ec, std::size_t size_a, nano::block_type type_a)
{
	auto node = connection->node.lock ();
	if (!node)
	{
		return;
	}
	if (!ec)
	{
		nano::bufferstream stream (receive_buffer->data (), size_a);
		auto block (nano::deserialize_block (stream, type_a));
		if (block != nullptr)
		{
			if (node->network_params.work.validate_entry (*block))
			{
				node->nlogger.debug (nano::log::type::bulk_push_server, "Insufficient work for bulk push block: {}", block->hash ().to_string ());
				node->stats.inc (nano::stat::type::error, nano::stat::detail::insufficient_work);
				return;
			}
			node->process_active (std::move (block));
			throttled_receive ();
		}
		else
		{
			node->nlogger.debug (nano::log::type::bulk_push_server, "Error deserializing block received from pull request");
		}
	}
}
