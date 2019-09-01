#include <nano/node/distributed_work.hpp>
#include <nano/node/node.hpp>

std::shared_ptr<request_type> nano::work_peer_request::get_prepared_json_request (std::string const & request_string_a) const
{
	auto request (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
	request->method (boost::beast::http::verb::post);
	request->set (boost::beast::http::field::content_type, "application/json");
	request->target ("/");
	request->version (11);
	request->body () = request_string_a;
	request->prepare_payload ();
	return request;
}

nano::distributed_work::distributed_work (unsigned int backoff_a, nano::node & node_a, nano::block_hash const & root_a, std::function<void(boost::optional<uint64_t>)> const & callback_a, uint64_t difficulty_a) :
callback (callback_a),
backoff (backoff_a),
node (node_a),
root (root_a),
need_resolve (node.config.work_peers),
difficulty (difficulty_a)
{
	assert (!completed);
}

nano::distributed_work::~distributed_work ()
{
	stop (true);
}

void nano::distributed_work::start ()
{
	if (need_resolve.empty ())
	{
		start_work ();
	}
	else
	{
		auto current (need_resolve.back ());
		need_resolve.pop_back ();
		auto this_l (shared_from_this ());
		boost::system::error_code ec;
		auto parsed_address (boost::asio::ip::address_v6::from_string (current.first, ec));
		if (!ec)
		{
			outstanding[parsed_address] = current.second;
			start ();
		}
		else
		{
			node.network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (current.first, std::to_string (current.second)), [current, this_l](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
				if (!ec)
				{
					for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
					{
						auto endpoint (i->endpoint ());
						this_l->outstanding[endpoint.address ()] = endpoint.port ();
					}
				}
				else
				{
					this_l->node.logger.try_log (boost::str (boost::format ("Error resolving work peer: %1%:%2%: %3%") % current.first % current.second % ec.message ()));
				}
				this_l->start ();
			});
		}
	}
}

void nano::distributed_work::start_work ()
{
	auto this_l (shared_from_this ());

	// Start work generation if peers are not acting correctly, or if there are no peers configured
	if ((outstanding.empty () || node.unresponsive_work_peers) && (node.config.work_threads != 0 || node.work.opencl))
	{
		local_generation_started = true;
		node.work.generate (
		root, [this_l](boost::optional<uint64_t> const & work_a) {
			this_l->set_once (work_a);
			this_l->stop (false);
		},
		difficulty);
	}

	if (!outstanding.empty ())
	{
		nano::lock_guard<std::mutex> guard (mutex);
		for (auto const & i : outstanding)
		{
			auto host (i.first);
			auto service (i.second);
			auto connection (std::make_shared<nano::work_peer_request> (this_l->node.io_ctx, host, service));
			connections.emplace_back (connection);
			connection->socket.async_connect (nano::tcp_endpoint (host, service), [this_l, connection](boost::system::error_code const & ec) {
				if (!ec)
				{
					std::string request_string;
					{
						boost::property_tree::ptree request;
						request.put ("action", "work_generate");
						request.put ("hash", this_l->root.to_string ());
						request.put ("difficulty", nano::to_string_hex (this_l->difficulty));
						std::stringstream ostream;
						boost::property_tree::write_json (ostream, request);
						request_string = ostream.str ();
					}
					auto request (connection->get_prepared_json_request (request_string));
					boost::beast::http::async_write (connection->socket, *request, [this_l, connection, request](boost::system::error_code const & ec, size_t bytes_transferred) {
						if (!ec)
						{
							boost::beast::http::async_read (connection->socket, connection->buffer, connection->response, [this_l, connection](boost::system::error_code const & ec, size_t bytes_transferred) {
								if (!ec)
								{
									if (connection->response.result () == boost::beast::http::status::ok)
									{
										this_l->success (connection->response.body (), connection->address);
									}
									else
									{
										this_l->node.logger.try_log (boost::str (boost::format ("Work peer responded with an error %1% %2%: %3%") % connection->address % connection->port % connection->response.result ()));
										this_l->failure (connection->address);
									}
								}
								else if (ec == boost::system::errc::operation_canceled)
								{
									// The only case where we send a cancel is if we preempt stopped waiting for the response
									this_l->cancel (connection);
									this_l->failure (connection->address);
								}
								else
								{
									this_l->node.logger.try_log (boost::str (boost::format ("Unable to read from work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ()));
									this_l->failure (connection->address);
								}
							});
						}
						else
						{
							this_l->node.logger.try_log (boost::str (boost::format ("Unable to write to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ()));
							this_l->failure (connection->address);
						}
					});
				}
				else
				{
					this_l->node.logger.try_log (boost::str (boost::format ("Unable to connect to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ()));
					this_l->failure (connection->address);
				}
			});
		}
	}
}

void nano::distributed_work::cancel (std::shared_ptr<nano::work_peer_request> connection)
{
	auto this_l (shared_from_this ());
	auto cancelling_l (std::make_shared<nano::work_peer_request> (node.io_ctx, connection->address, connection->port));
	cancelling_l->socket.async_connect (nano::tcp_endpoint (cancelling_l->address, cancelling_l->port), [this_l, cancelling_l](boost::system::error_code const & ec) {
		if (!ec)
		{
			std::string request_string;
			{
				boost::property_tree::ptree request;
				request.put ("action", "work_cancel");
				request.put ("hash", this_l->root.to_string ());
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, request);
				request_string = ostream.str ();
			}
			auto request (cancelling_l->get_prepared_json_request (request_string));
			boost::beast::http::async_write (cancelling_l->socket, *request, [this_l, request, cancelling_l](boost::system::error_code const & ec, size_t bytes_transferred) {
				if (ec)
				{
					this_l->node.logger.try_log (boost::str (boost::format ("Unable to send work_cancel to work_peer %1% %2%: %3% (%4%)") % cancelling_l->address % cancelling_l->port % ec.message () % ec.value ()));
				}
			});
		}
	});
}

void nano::distributed_work::stop (bool const local_stop_a)
{
	if (!stopped.exchange (true))
	{
		nano::lock_guard<std::mutex> guard (mutex);
		if (local_stop_a && (node.config.work_threads != 0 || node.work.opencl))
		{
			node.work.cancel (root);
		}
		for (auto & connection_w : connections)
		{
			if (auto connection_l = connection_w.lock ())
			{
				boost::system::error_code ec;
				connection_l->socket.cancel (ec);
				if (ec)
				{
					node.logger.try_log (boost::str (boost::format ("Error cancelling operation with work_peer %1% %2%: %3%") % connection_l->address % connection_l->port % ec.message () % ec.value ()));
				}
				try
				{
					connection_l->socket.close ();
				}
				catch (const boost::system::system_error & ec)
				{
					node.logger.try_log (boost::str (boost::format ("Error closing socket with work_peer %1% %2%: %3%") % connection_l->address % connection_l->port % ec.what () % ec.code ()));
				}
			}
		}
		connections.clear ();
		outstanding.clear ();
	}
}

void nano::distributed_work::success (std::string const & body_a, boost::asio::ip::address const & address)
{
	auto last (remove (address));
	std::stringstream istream (body_a);
	try
	{
		boost::property_tree::ptree result;
		boost::property_tree::read_json (istream, result);
		auto work_text (result.get<std::string> ("work"));
		uint64_t work;
		if (!nano::from_string_hex (work_text, work))
		{
			uint64_t result_difficulty (0);
			if (!nano::work_validate (root, work, &result_difficulty) && result_difficulty >= difficulty)
			{
				node.unresponsive_work_peers = false;
				set_once (work);
				stop (true);
			}
			else
			{
				node.logger.try_log (boost::str (boost::format ("Incorrect work response from %1% for root %2% with diffuculty %3%: %4%") % address % root.to_string () % nano::to_string_hex (difficulty) % work_text));
				handle_failure (last);
			}
		}
		else
		{
			node.logger.try_log (boost::str (boost::format ("Work response from %1% wasn't a number: %2%") % address % work_text));
			handle_failure (last);
		}
	}
	catch (...)
	{
		node.logger.try_log (boost::str (boost::format ("Work response from %1% wasn't parsable: %2%") % address % body_a));
		handle_failure (last);
	}
}

void nano::distributed_work::set_once (boost::optional<uint64_t> work_a)
{
	if (!completed.exchange (true))
	{
		callback (work_a);
	}
}

void nano::distributed_work::failure (boost::asio::ip::address const & address)
{
	auto last (remove (address));
	handle_failure (last);
}

void nano::distributed_work::handle_failure (bool const last)
{
	if (last && !completed)
	{
		node.unresponsive_work_peers = true;
		if (!local_generation_started)
		{
			if (stopped)
			{
				callback (boost::none);
			}
			else
			{
				if (backoff == 1 && node.config.logging.work_generation_time ())
				{
					node.logger.always_log ("Work peer(s) failed to generate work for root ", root.to_string (), ", retrying...");
				}
				auto now (std::chrono::steady_clock::now ());
				auto root_l (root);
				auto callback_l (callback);
				std::weak_ptr<nano::node> node_w (node.shared ());
				auto next_backoff (std::min (backoff * 2, (unsigned int)60 * 5));
				// clang-format off
				node.alarm.add (now + std::chrono::seconds (backoff), [ node_w, root_l, callback_l, next_backoff, difficulty = difficulty ] {
					if (auto node_l = node_w.lock ())
					{
						node_l->distributed_work.make (next_backoff, root_l, callback_l, difficulty);
					}
				});
				// clang-format on
			}
		}
	}
}

bool nano::distributed_work::remove (boost::asio::ip::address const & address)
{
	nano::lock_guard<std::mutex> guard (mutex);
	outstanding.erase (address);
	return outstanding.empty ();
}

nano::distributed_work_factory::distributed_work_factory (nano::node & node_a) :
node (node_a)
{
}

void nano::distributed_work_factory::make (nano::block_hash const & root_a, std::function<void(boost::optional<uint64_t>)> const & callback_a, uint64_t difficulty_a)
{
	make (1, root_a, callback_a, difficulty_a);
}

void nano::distributed_work_factory::make (unsigned int backoff_a, nano::block_hash const & root_a, std::function<void(boost::optional<uint64_t>)> const & callback_a, uint64_t difficulty_a)
{
	cleanup_finished ();
	auto distributed (std::make_shared<nano::distributed_work> (backoff_a, node, root_a, callback_a, difficulty_a));
	{
		nano::lock_guard<std::mutex> guard (mutex);
		work[root_a].emplace_back (distributed);
	}
	distributed->start ();
}

void nano::distributed_work_factory::cancel (nano::block_hash const & root_a, bool const local_stop)
{
	{
		nano::lock_guard<std::mutex> guard (mutex);
		auto existing_l (work.find (root_a));
		if (existing_l != work.end ())
		{
			for (auto & distributed_w : existing_l->second)
			{
				if (auto distributed_l = distributed_w.lock ())
				{
					// Send work_cancel to work peers
					// Cancels local generation if local_stop is true, but usually should be done by the work pool
					distributed_l->stop (local_stop);
				}
			}
			work.erase (existing_l);
		}
	}
}

void nano::distributed_work_factory::cleanup_finished ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	for (auto it (work.begin ()), end (work.end ()); it != end;)
	{
		it->second.erase (std::remove_if (it->second.begin (), it->second.end (), [](auto distributed_a) {
			return distributed_a.expired ();
		}),
		it->second.end ());

		if (it->second.empty ())
		{
			it = work.erase (it);
		}
		else
		{
			++it;
		}
	}
}
