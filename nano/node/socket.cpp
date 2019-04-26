#include <nano/node/node.hpp>
#include <nano/node/socket.hpp>

nano::socket::socket (std::shared_ptr<nano::node> node_a, boost::optional<std::chrono::seconds> max_idle_time_a, nano::socket::concurrency concurrency_a) :
strand (node_a->io_ctx.get_executor ()),
tcp_socket (node_a->io_ctx),
node (node_a),
deadline (node_a->io_ctx),
max_idle_time (max_idle_time_a),
writer_concurrency (concurrency_a)
{
	if (!max_idle_time)
	{
		max_idle_time = node_a->config.tcp_idle_timeout;
	}
}

void nano::socket::async_connect (nano::tcp_endpoint const & endpoint_a, std::function<void(boost::system::error_code const &)> callback_a)
{
	auto this_l (shared_from_this ());
	start_timer ();
	this_l->tcp_socket.async_connect (endpoint_a,
	boost::asio::bind_executor (this_l->strand,
	[this_l, callback_a, endpoint_a](boost::system::error_code const & ec) {
		this_l->stop_timer ();
		this_l->remote = endpoint_a;
		callback_a (ec);
	}));
}

void nano::socket::async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	assert (size_a <= buffer_a->size ());
	auto this_l (shared_from_this ());
	start_timer ();
	boost::asio::async_read (tcp_socket, boost::asio::buffer (buffer_a->data (), size_a),
	boost::asio::bind_executor (strand,
	[this_l, buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node = this_l->node.lock ())
		{
			node->stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::in, size_a);
			this_l->stop_timer ();
			callback_a (ec, size_a);
		}
	}));
}

void nano::socket::async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	auto this_l (shared_from_this ());
	if (writer_concurrency == nano::socket::concurrency::multi_writer)
	{
		boost::asio::post (strand,
		[buffer_a, callback_a, this_l]() {
			bool write_in_progress = !this_l->send_queue.empty ();
			this_l->send_queue.emplace_back (nano::socket::queue_item{ buffer_a, callback_a });
			if (!write_in_progress)
			{
				this_l->write_queued_messages ();
			}
		});
	}
	else
	{
		start_timer ();
		boost::asio::async_write (tcp_socket, boost::asio::buffer (buffer_a->data (), buffer_a->size ()),
		boost::asio::bind_executor (strand,
		[this_l, buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
			if (auto node = this_l->node.lock ())
			{
				node->stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::out, size_a);
				this_l->stop_timer ();
				callback_a (ec, size_a);
			}
		}));
	}
}

void nano::socket::write_queued_messages ()
{
	std::weak_ptr<nano::socket> this_w (shared_from_this ());
	auto msg (send_queue.front ());
	start_timer ();
	boost::asio::async_write (tcp_socket, boost::asio::buffer (msg.buffer->data (), msg.buffer->size ()),
	boost::asio::bind_executor (strand,
	[msg, this_w](boost::system::error_code ec, std::size_t size_a) {
		if (auto this_l = this_w.lock ())
		{
			if (auto node = this_l->node.lock ())
			{
				node->stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::out, size_a);
			}
			this_l->stop_timer ();

			if (!this_l->closed)
			{
				if (msg.callback)
				{
					msg.callback (ec, size_a);
				}

				this_l->send_queue.pop_front ();
				if (!ec && !this_l->send_queue.empty ())
				{
					this_l->write_queued_messages ();
				}
			}
		}
	}));
}

void nano::socket::start_timer ()
{
	if (auto node_l = node.lock ())
	{
		start_timer (node_l->config.tcp_io_timeout);
	}
}

void nano::socket::start_timer (std::chrono::seconds deadline_a)
{
	std::weak_ptr<nano::socket> this_w (shared_from_this ());
	boost::asio::post (strand, [this_w, deadline_a]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->deadline.expires_after (deadline_a);
			this_l->deadline.async_wait (boost::asio::bind_executor (this_l->strand, [this_l](boost::system::error_code const & ec_a) {
				if (!ec_a)
				{
					if (auto node_l = this_l->node.lock ())
					{
						this_l->timed_out = true;
						this_l->close ();
						if (node_l->config.logging.network_timeout_logging ())
						{
							node_l->logger.try_log (boost::str (boost::format ("Disconnecting from %1% due to timeout") % this_l->remote_endpoint ()));
						}
					}
				}
			}));
		}
	});
}

void nano::socket::stop_timer ()
{
	auto this_l (shared_from_this ());
	boost::asio::dispatch (strand, [this_l]() {
		if (this_l->max_idle_time && !this_l->closed)
		{
			this_l->start_timer (*this_l->max_idle_time);
		}
		else
		{
			this_l->deadline.cancel ();
		}
	});
}

bool nano::socket::has_timed_out () const
{
	return timed_out;
}

void nano::socket::set_max_idle_timeout (std::chrono::seconds max_idle_time_a)
{
	auto this_l (shared_from_this ());
	boost::asio::dispatch (strand, [this_l, max_idle_time_a]() {
		this_l->max_idle_time = max_idle_time_a;
	});
}

// This must be called from a strand
void nano::socket::close_internal ()
{
	if (!closed)
	{
		closed = true;
		deadline.cancel ();
		max_idle_time = boost::none;
		boost::system::error_code ec;

		// Ignore error code for shutdown as it is best-effort
		tcp_socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ec);
		tcp_socket.close (ec);
		send_queue.clear ();
		if (ec)
		{
			if (auto node_l = node.lock ())
			{
				node_l->logger.try_log ("Failed to close socket gracefully: ", ec.message ());
				node_l->stats.inc (nano::stat::type::bootstrap, nano::stat::detail::error_socket_close);
			}
		}
	}
}

void nano::socket::close ()
{
	auto this_l (shared_from_this ());
	boost::asio::dispatch (strand, [this_l] {
		this_l->close_internal ();
	});
}

nano::tcp_endpoint nano::socket::remote_endpoint () const
{
	return remote;
}

void nano::socket::set_writer_concurrency (concurrency writer_concurrency_a)
{
	writer_concurrency = writer_concurrency_a;
}

nano::server_socket::server_socket (std::shared_ptr<nano::node> node_a, boost::asio::ip::tcp::endpoint local_a, size_t max_connections_a, nano::socket::concurrency concurrency_a) :
socket (node_a, std::chrono::seconds::max (), concurrency_a), acceptor (node_a->io_ctx), local (local_a), deferred_accept_timer (node_a->io_ctx), max_inbound_connections (max_connections_a), concurrency_new_connections (concurrency_a)
{
}

void nano::server_socket::start (boost::system::error_code & ec_a)
{
	acceptor.open (local.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
	acceptor.bind (local, ec_a);
	if (!ec_a)
	{
		acceptor.listen (boost::asio::socket_base::max_listen_connections, ec_a);
	}
}

void nano::server_socket::close ()
{
	auto this_l (std::static_pointer_cast<nano::server_socket> (shared_from_this ()));

	boost::asio::dispatch (strand, [this_l]() {
		this_l->close_internal ();
		this_l->acceptor.close ();
		for (auto & connection_w : this_l->connections)
		{
			if (auto connection_l = connection_w.lock ())
			{
				connection_l->close ();
			}
		}
		this_l->connections.clear ();
	});
}

void nano::server_socket::on_connection (std::function<bool(std::shared_ptr<nano::socket>, boost::system::error_code const &)> callback_a)
{
	auto this_l (std::static_pointer_cast<nano::server_socket> (shared_from_this ()));

	boost::asio::post (strand, [this_l, callback_a]() {
		if (auto node_l = this_l->node.lock ())
		{
			if (this_l->acceptor.is_open ())
			{
				if (this_l->connections.size () < this_l->max_inbound_connections)
				{
					// Prepare new connection
					auto new_connection (std::make_shared<nano::socket> (node_l->shared (), boost::none, this_l->concurrency_new_connections));

					this_l->acceptor.async_accept (new_connection->tcp_socket, new_connection->remote,
					boost::asio::bind_executor (this_l->strand,
					[this_l, new_connection, callback_a](boost::system::error_code const & ec_a) {
						if (auto node_l = this_l->node.lock ())
						{
							if (!ec_a)
							{
								// Make sure the new connection doesn't idle. Note that in most cases, the callback is going to start
								// an IO operation immediately, which will start a timer.
								new_connection->start_timer (node_l->config.tcp_idle_timeout);
								node_l->stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
								this_l->connections.push_back (new_connection);
								this_l->evict_dead_connections ();
							}
							else
							{
								node_l->logger.try_log ("Unable to accept connection: ", ec_a.message ());
							}

							// If the callback returns true, keep accepting new connections
							if (callback_a (new_connection, ec_a))
							{
								this_l->on_connection (callback_a);
							}
							else
							{
								node_l->logger.try_log ("Stopping to accept connections");
							}
						}
					}));
				}
				else
				{
					this_l->evict_dead_connections ();
					node_l->stats.inc (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in);
					this_l->deferred_accept_timer.expires_after (std::chrono::seconds (2));
					this_l->deferred_accept_timer.async_wait ([this_l, callback_a](const boost::system::error_code & ec_a) {
						if (!ec_a)
						{
							// Try accepting again
							std::static_pointer_cast<nano::server_socket> (this_l)->on_connection (callback_a);
						}
						else
						{
							if (auto node_l = this_l->node.lock ())
							{
								node_l->logger.try_log ("Unable to accept connection (deferred): ", ec_a.message ());
							}
						}
					});
				}
			}
		}
	});
}

// This must be called from a strand
void nano::server_socket::evict_dead_connections ()
{
	auto it = connections.begin ();
	for (; it != connections.end (); it++)
	{
		if (it->expired ())
		{
			it = connections.erase (it);
		}
	}
}
