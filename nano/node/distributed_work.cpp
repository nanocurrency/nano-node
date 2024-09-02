#include <nano/boost/asio/bind_executor.hpp>
#include <nano/node/distributed_work.hpp>
#include <nano/node/node.hpp>
#include <nano/node/websocket.hpp>

#include <boost/algorithm/string/erase.hpp>

std::shared_ptr<request_type> nano::distributed_work::peer_request::get_prepared_json_request (std::string const & request_string_a) const
{
	auto http_request = std::make_shared<request_type> ();
	http_request->method (boost::beast::http::verb::post);
	http_request->set (boost::beast::http::field::content_type, "application/json");
	auto address_string = boost::algorithm::erase_first_copy (endpoint.address ().to_string (), "::ffff:");
	http_request->set (boost::beast::http::field::host, address_string);
	http_request->target ("/");
	http_request->version (11);
	http_request->body () = request_string_a;
	http_request->prepare_payload ();
	return http_request;
}

nano::distributed_work::distributed_work (nano::node & node_a, nano::work_request const & request_a, std::chrono::seconds const & backoff_a) :
	node (node_a),
	node_w (node_a.shared ()),
	request (request_a),
	backoff (backoff_a),
	strand (node_a.io_ctx.get_executor ()),
	need_resolve (request_a.peers),
	elapsed (nano::timer_state::started, "distributed work generation timer")
{
	debug_assert (!finished);
	debug_assert (status == work_generation_status::ongoing);
}

nano::distributed_work::~distributed_work ()
{
	debug_assert (status != work_generation_status::ongoing);
	if (auto node_l = node_w.lock ())
	{
		if (!node_l->stopped && node_l->websocket.server && node_l->websocket.server->any_subscriber (nano::websocket::topic::work))
		{
			nano::websocket::message_builder builder;
			if (status == work_generation_status::success)
			{
				node_l->websocket.server->broadcast (builder.work_generation (request.version, request.root.as_block_hash (), work_result, request.difficulty, node_l->default_difficulty (request.version), elapsed.value (), winner, bad_peers));
			}
			else if (status == work_generation_status::cancelled)
			{
				node_l->websocket.server->broadcast (builder.work_cancelled (request.version, request.root.as_block_hash (), request.difficulty, node_l->default_difficulty (request.version), elapsed.value (), bad_peers));
			}
			else if (status == work_generation_status::failure_local || status == work_generation_status::failure_peers)
			{
				node_l->websocket.server->broadcast (builder.work_failed (request.version, request.root.as_block_hash (), request.difficulty, node_l->default_difficulty (request.version), elapsed.value (), bad_peers));
			}
		}
		stop_once (true);
	}
}

void nano::distributed_work::start ()
{
	// Start work generation if peers are not acting correctly, or if there are no peers configured
	if ((need_resolve.empty () || node.unresponsive_work_peers) && node.local_work_generation_enabled ())
	{
		start_local ();
	}
	// Fallback when local generation is required but it is not enabled is to simply call the callback with an error
	else if (need_resolve.empty () && request.callback)
	{
		status = work_generation_status::failure_local;
		request.callback (std::nullopt);
	}
	for (auto const & peer : need_resolve)
	{
		boost::system::error_code ec;
		auto parsed_address (boost::asio::ip::make_address_v6 (peer.first, ec));
		if (!ec)
		{
			do_request (nano::tcp_endpoint (parsed_address, peer.second));
		}
		else
		{
			auto this_l (shared_from_this ());
			node.network.resolver.async_resolve (boost::asio::ip::tcp::resolver::query (peer.first, std::to_string (peer.second)), [peer, this_l, &extra = resolved_extra] (boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
				if (!ec)
				{
					this_l->do_request (nano::tcp_endpoint (i_a->endpoint ().address (), i_a->endpoint ().port ()));
					++i_a;
					for (auto & i : boost::make_iterator_range (i_a, {}))
					{
						++extra;
						this_l->do_request (nano::tcp_endpoint (i.endpoint ().address (), i.endpoint ().port ()));
					}
				}
				else
				{
					this_l->node.logger.error (nano::log::type::distributed_work, "Error resolving work peer: {}:{} ({})", peer.first, peer.second, ec.message ());

					this_l->failure ();
				}
			});
		}
	}
}

void nano::distributed_work::start_local ()
{
	auto this_l (shared_from_this ());
	local_generation_started = true;
	node.work.generate (request.version, request.root, request.difficulty, [this_l] (boost::optional<uint64_t> const & work_a) {
		if (work_a.is_initialized ())
		{
			this_l->set_once (*work_a);
		}
		else if (!this_l->finished.exchange (true))
		{
			this_l->status = work_generation_status::failure_local;
			if (this_l->request.callback)
			{
				this_l->request.callback (std::nullopt);
			}
		}
		this_l->stop_once (false);
	});
}

void nano::distributed_work::do_request (nano::tcp_endpoint const & endpoint_a)
{
	auto this_l (shared_from_this ());
	auto connection (std::make_shared<peer_request> (node.io_ctx, endpoint_a));
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		connections.emplace_back (connection);
	}
	connection->socket.async_connect (connection->endpoint,
	boost::asio::bind_executor (strand,
	[this_l, connection] (boost::system::error_code const & ec) {
		if (!ec && !this_l->stopped)
		{
			std::string request_string;
			{
				boost::property_tree::ptree rpc_request;
				rpc_request.put ("action", "work_generate");
				rpc_request.put ("hash", this_l->request.root.to_string ());
				rpc_request.put ("difficulty", nano::to_string_hex (this_l->request.difficulty));
				if (this_l->request.account.has_value ())
				{
					rpc_request.put ("account", this_l->request.account.value ().to_account ());
				}
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, rpc_request);
				request_string = ostream.str ();
			}
			auto peer_request (connection->get_prepared_json_request (request_string));
			boost::beast::http::async_write (connection->socket, *peer_request,
			boost::asio::bind_executor (this_l->strand,
			[this_l, connection, peer_request] (boost::system::error_code const & ec, std::size_t size_a) {
				if (!ec && !this_l->stopped)
				{
					boost::beast::http::async_read (connection->socket, connection->buffer, connection->response,
					boost::asio::bind_executor (this_l->strand, [this_l, connection] (boost::system::error_code const & ec, std::size_t size_a) {
						if (!ec && !this_l->stopped)
						{
							if (connection->response.result () == boost::beast::http::status::ok)
							{
								this_l->success (connection->response.body (), connection->endpoint);
							}
							else if (ec)
							{
								this_l->node.logger.error (nano::log::type::distributed_work, "Work peer responded with an error {}:{} ({})",
								nano::util::to_str (connection->endpoint.address ()),
								connection->endpoint.port (),
								ec.message ());

								this_l->add_bad_peer (connection->endpoint);
								this_l->failure ();
							}
						}
						else if (ec)
						{
							this_l->do_cancel (connection->endpoint);
							this_l->failure ();
						}
					}));
				}
				else if (ec && ec != boost::system::errc::operation_canceled)
				{
					this_l->node.logger.error (nano::log::type::distributed_work, "Unable to write to work peer {}:{} ({})",
					nano::util::to_str (connection->endpoint.address ()),
					connection->endpoint.port (),
					ec.message ());

					this_l->add_bad_peer (connection->endpoint);
					this_l->failure ();
				}
			}));
		}
		else if (ec && ec != boost::system::errc::operation_canceled)
		{
			this_l->node.logger.error (nano::log::type::distributed_work, "Unable to connect to work peer {}:{} ({})",
			nano::util::to_str (connection->endpoint.address ()),
			connection->endpoint.port (),
			ec.message ());

			this_l->add_bad_peer (connection->endpoint);
			this_l->failure ();
		}
	}));
}

void nano::distributed_work::do_cancel (nano::tcp_endpoint const & endpoint_a)
{
	auto this_l (shared_from_this ());
	auto cancelling_l (std::make_shared<peer_request> (node.io_ctx, endpoint_a));
	cancelling_l->socket.async_connect (cancelling_l->endpoint,
	boost::asio::bind_executor (strand,
	[this_l, cancelling_l] (boost::system::error_code const & ec) {
		if (!ec)
		{
			std::string request_string;
			{
				boost::property_tree::ptree rpc_request;
				rpc_request.put ("action", "work_cancel");
				rpc_request.put ("hash", this_l->request.root.to_string ());
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, rpc_request);
				request_string = ostream.str ();
			}
			auto peer_cancel (cancelling_l->get_prepared_json_request (request_string));
			boost::beast::http::async_write (cancelling_l->socket, *peer_cancel,
			boost::asio::bind_executor (this_l->strand,
			[this_l, peer_cancel, cancelling_l] (boost::system::error_code const & ec, std::size_t bytes_transferred) {
				if (ec && ec != boost::system::errc::operation_canceled)
				{
					this_l->node.logger.error (nano::log::type::distributed_work, "Unable to send work cancel to work peer {}:{} ({})",
					nano::util::to_str (cancelling_l->endpoint.address ()),
					cancelling_l->endpoint.port (),
					ec.message ());
				}
			}));
		}
	}));
}

void nano::distributed_work::success (std::string const & body_a, nano::tcp_endpoint const & endpoint_a)
{
	bool error = true;
	try
	{
		std::stringstream istream (body_a);
		boost::property_tree::ptree result;
		boost::property_tree::read_json (istream, result);
		auto work_text (result.get<std::string> ("work"));
		uint64_t work;
		if (!nano::from_string_hex (work_text, work))
		{
			if (nano::dev::network_params.work.difficulty (request.version, request.root, work) >= request.difficulty)
			{
				error = false;
				node.unresponsive_work_peers = false;
				set_once (work, boost::str (boost::format ("%1%:%2%") % endpoint_a.address () % endpoint_a.port ()));
				stop_once (true);
			}
			else
			{
				node.logger.error (nano::log::type::distributed_work, "Incorrect work response from {}:{} for root {} with difficulty {}: {}",
				nano::util::to_str (endpoint_a.address ()),
				endpoint_a.port (),
				request.root.to_string (),
				nano::to_string_hex (request.difficulty),
				work_text);
			}
		}
		else
		{
			node.logger.error (nano::log::type::distributed_work, "Work response from {}:{} wasn't a number: {}",
			nano::util::to_str (endpoint_a.address ()),
			endpoint_a.port (),
			work_text);
		}
	}
	catch (...)
	{
		node.logger.error (nano::log::type::distributed_work, "Work response from {}:{} wasn't parsable: {}",
		nano::util::to_str (endpoint_a.address ()),
		endpoint_a.port (),
		body_a);
	}
	if (error)
	{
		add_bad_peer (endpoint_a);
		failure ();
	}
}

void nano::distributed_work::stop_once (bool const local_stop_a)
{
	if (!stopped.exchange (true))
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (local_stop_a && node.local_work_generation_enabled ())
		{
			node.work.cancel (request.root);
		}
		for (auto & connection_w : connections)
		{
			if (auto connection_l = connection_w.lock ())
			{
				auto this_l (shared_from_this ());
				boost::asio::post (strand, boost::asio::bind_executor (strand, [this_l, connection_l] {
					boost::system::error_code ec;
					if (connection_l->socket.is_open ())
					{
						connection_l->socket.cancel (ec);
						if (!ec)
						{
							connection_l->socket.close (ec);
							if (ec)
							{
								this_l->node.logger.error (nano::log::type::distributed_work, "Error closing socket with work peer: {}:{} ({})",
								nano::util::to_str (connection_l->endpoint.address ()),
								connection_l->endpoint.port (),
								ec.message ());
							}
						}
						else
						{
							this_l->node.logger.error (nano::log::type::distributed_work, "Error cancelling operation with work peer: {}:{} ({})",
							nano::util::to_str (connection_l->endpoint.address ()),
							connection_l->endpoint.port (),
							ec.message ());
						}
					}
				}));
			}
		}
		connections.clear ();
	}
}

void nano::distributed_work::set_once (uint64_t const work_a, std::string const & source_a)
{
	if (!finished.exchange (true))
	{
		elapsed.stop ();

		node.logger.info (nano::log::type::distributed_work, "Work generation for {}, with a threshold difficulty of {} (multiplier {}x) complete: {} ms",
		request.root.to_string (),
		nano::to_string_hex (request.difficulty),
		nano::to_string (nano::difficulty::to_multiplier (request.difficulty, node.default_difficulty (request.version)), 2),
		elapsed.value ().count ());

		status = work_generation_status::success;
		if (request.callback)
		{
			request.callback (work_a);
		}
		winner = source_a;
		work_result = work_a;
	}
}

void nano::distributed_work::cancel ()
{
	if (!finished.exchange (true))
	{
		elapsed.stop ();

		node.logger.info (nano::log::type::distributed_work, "Work generation for {} was cancelled after {} ms",
		request.root.to_string (),
		elapsed.value ().count ());

		status = work_generation_status::cancelled;
		if (request.callback)
		{
			request.callback (std::nullopt);
		}
		stop_once (true);
	}
}

void nano::distributed_work::failure ()
{
	if (++failures == need_resolve.size () + resolved_extra.load ())
	{
		handle_failure ();
	}
}

void nano::distributed_work::handle_failure ()
{
	if (!finished)
	{
		node.unresponsive_work_peers = true;
		if (!local_generation_started && !finished.exchange (true))
		{
			node.logger.info (nano::log::type::distributed_work, "Work peer(s) failed to generate work for root {}, retrying... (backoff: {}s)",
			request.root.to_string (),
			backoff.count ());

			status = work_generation_status::failure_peers;

			auto now (std::chrono::steady_clock::now ());
			std::weak_ptr<nano::node> node_weak (node.shared ());
			auto next_backoff (std::min (backoff * 2, std::chrono::seconds (5 * 60)));
			node.workers.add_timed_task (now + std::chrono::seconds (backoff), [node_weak, request_l = request, next_backoff] {
				bool error_l{ true };
				if (auto node_l = node_weak.lock ())
				{
					error_l = node_l->distributed_work.make (next_backoff, request_l);
				}
				if (error_l && request_l.callback)
				{
					request_l.callback (std::nullopt);
				}
			});
		}
		else
		{
			// wait for local work generation to complete
		}
	}
}

void nano::distributed_work::add_bad_peer (nano::tcp_endpoint const & endpoint_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	bad_peers.emplace_back (boost::str (boost::format ("%1%:%2%") % endpoint_a.address () % endpoint_a.port ()));
}
