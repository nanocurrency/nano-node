#include <algorithm>
#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <nano/node/node.hpp>
#include <nano/node/websocket.hpp>

nano::websocket::confirmation_options::confirmation_options (boost::property_tree::ptree const & options_a) :
all_local_accounts (options_a.get<bool> ("all_local_accounts", false))
{
	auto accounts_l (options_a.get_child_optional ("accounts"));
	if (accounts_l)
	{
		for (auto account : *accounts_l)
		{
			// Check if the account is valid, but no error handling if it's not, simply not added to the filter
			nano::account result (0);
			if (!result.decode_account (account.second.data ()))
			{
				accounts.insert (account.second.data ());
			}
		}
	}
}

bool nano::websocket::confirmation_options::filter (boost::property_tree::ptree const & message_a, nano::node & node_a) const
{
	// If this fails, the message builder has been changed
	auto account_text (message_a.get<std::string> ("message.account"));
	if (all_local_accounts)
	{
		auto transaction (node_a.wallets.tx_begin_read ());
		nano::account account (0);
		assert (account.decode_account (account_text));
		if (node_a.wallets.exists (transaction, account))
		{
			return false;
		}
	}
	if (accounts.find (account_text) != accounts.end ())
	{
		return false;
	}
	return true;
}

nano::websocket::session::session (nano::websocket::listener & listener_a, boost::asio::ip::tcp::socket socket_a) :
ws_listener (listener_a), ws (std::move (socket_a)), write_strand (ws.get_executor ())
{
	ws.text (true);
	ws_listener.get_node ().logger.try_log ("websocket session started");
}

nano::websocket::session::~session ()
{
	{
		std::unique_lock<std::mutex> lk (subscriptions_mutex);
		for (auto & subscription : subscriptions)
		{
			ws_listener.decrease_subscription_count (subscription.first);
		}
	}

	ws_listener.get_node ().logger.try_log ("websocket session ended");
}

void nano::websocket::session::handshake ()
{
	std::lock_guard<std::mutex> lk (io_mutex);
	ws.async_accept ([self_l = shared_from_this ()](boost::system::error_code const & ec) {
		if (!ec)
		{
			// Start reading incoming messages
			self_l->read ();
		}
		else
		{
			self_l->ws_listener.get_node ().logger.always_log ("websocket handshake failed: ", ec.message ());
		}
	});
}

void nano::websocket::session::close ()
{
	std::lock_guard<std::mutex> lk (io_mutex);
	boost::beast::websocket::close_reason reason;
	reason.code = boost::beast::websocket::close_code::normal;
	reason.reason = "Shutting down";
	boost::system::error_code ec_ignore;
	ws.close (reason, ec_ignore);
}

void nano::websocket::session::write (nano::websocket::message message_a, nano::node & node_a)
{
	// clang-format off
	std::unique_lock<std::mutex> lk (subscriptions_mutex);
	auto subscription (subscriptions.find (message_a.topic));
	if (message_a.topic == nano::websocket::topic::ack || (subscription != subscriptions.end () && !subscription->second->filter (message_a.contents, node_a)))
	{
		lk.unlock ();
		boost::asio::post (write_strand,
		[message_a, self_l = shared_from_this ()]() {
			bool write_in_progress = !self_l->send_queue.empty ();
			self_l->send_queue.emplace_back (message_a);
			if (!write_in_progress)
			{
				self_l->write_queued_messages ();
			}
		});
	}
	// clang-format on
}

void nano::websocket::session::write_queued_messages ()
{
	// clang-format off
	auto msg (send_queue.front ());
	auto msg_str (msg.to_string ());

	std::lock_guard<std::mutex> lk (io_mutex);
	ws.async_write (boost::asio::buffer (msg_str.data (), msg_str.size ()),
	boost::asio::bind_executor (write_strand,
	[msg, self_l = shared_from_this ()](boost::system::error_code ec, std::size_t bytes_transferred) {
		self_l->send_queue.pop_front ();
		if (!ec)
		{
			if (!self_l->send_queue.empty ())
			{
				self_l->write_queued_messages ();
			}
		}
	}));
	// clang-format on
}

void nano::websocket::session::read ()
{
	std::lock_guard<std::mutex> lk (io_mutex);
	ws.async_read (read_buffer,
	[self_l = shared_from_this ()](boost::system::error_code ec, std::size_t bytes_transferred) {
		if (!ec)
		{
			std::stringstream os;
			os << boost::beast::buffers (self_l->read_buffer.data ());
			std::string incoming_message = os.str ();

			// Prepare next read by clearing the multibuffer
			self_l->read_buffer.consume (self_l->read_buffer.size ());

			boost::property_tree::ptree tree_msg;
			try
			{
				boost::property_tree::read_json (os, tree_msg);
				self_l->handle_message (tree_msg);
				self_l->read ();
			}
			catch (boost::property_tree::json_parser::json_parser_error const & ex)
			{
				self_l->ws_listener.get_node ().logger.try_log ("websocket json parsing failed: ", ex.what ());
			}
		}
		else
		{
			self_l->ws_listener.get_node ().logger.try_log ("websocket read failed: ", ec.message ());
		}
	});
}

namespace
{
nano::websocket::topic to_topic (std::string topic_a)
{
	nano::websocket::topic topic = nano::websocket::topic::invalid;
	if (topic_a == "confirmation")
	{
		topic = nano::websocket::topic::confirmation;
	}
	else if (topic_a == "ack")
	{
		topic = nano::websocket::topic::ack;
	}
	return topic;
}

std::string from_topic (nano::websocket::topic topic_a)
{
	std::string topic = "invalid";
	if (topic_a == nano::websocket::topic::confirmation)
	{
		topic = "confirmation";
	}
	else if (topic_a == nano::websocket::topic::ack)
	{
		topic = "ack";
	}
	return topic;
}
}

void nano::websocket::session::send_ack (std::string action_a, std::string id_a)
{
	auto milli_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
	nano::websocket::message msg (nano::websocket::topic::ack);
	boost::property_tree::ptree & message_l = msg.contents;
	message_l.add ("ack", action_a);
	message_l.add ("time", std::to_string (milli_since_epoch));
	if (!id_a.empty ())
	{
		message_l.add ("id", id_a);
	}
	write (msg, ws_listener.get_node ());
}

void nano::websocket::session::handle_message (boost::property_tree::ptree const & message_a)
{
	std::string action (message_a.get<std::string> ("action", ""));
	auto topic_l (to_topic (message_a.get<std::string> ("topic", "")));
	auto ack_l (message_a.get<bool> ("ack", false));
	auto id_l (message_a.get<std::string> ("id", ""));
	auto action_succeeded (false);
	if (action == "subscribe" && topic_l != nano::websocket::topic::invalid)
	{
		auto options_l (message_a.get_child_optional ("options"));
		std::lock_guard<std::mutex> lk (subscriptions_mutex);
		if (topic_l == nano::websocket::topic::confirmation)
		{
			subscriptions.insert (std::make_pair (topic_l, options_l ? std::make_unique<nano::websocket::confirmation_options> (options_l.get ()) : std::make_unique<nano::websocket::options> ()));
		}
		else
		{
			subscriptions.insert (std::make_pair (topic_l, std::make_unique<nano::websocket::options> ()));
		}
		ws_listener.increase_subscription_count (topic_l);
		action_succeeded = true;
	}
	else if (action == "unsubscribe" && topic_l != nano::websocket::topic::invalid)
	{
		std::lock_guard<std::mutex> lk (subscriptions_mutex);
		if (subscriptions.erase (topic_l))
		{
			ws_listener.decrease_subscription_count (topic_l);
		}
		action_succeeded = true;
	}
	if (ack_l && action_succeeded)
	{
		send_ack (action, id_l);
	}
}

void nano::websocket::listener::stop ()
{
	stopped = true;
	acceptor.close ();

	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			session_ptr->close ();
		}
	}
}

nano::websocket::listener::listener (nano::node & node_a, boost::asio::ip::tcp::endpoint endpoint_a) :
node (node_a),
acceptor (node_a.io_ctx),
socket (node_a.io_ctx)
{
	try
	{
		acceptor.open (endpoint_a.protocol ());
		acceptor.set_option (boost::asio::socket_base::reuse_address (true));
		acceptor.bind (endpoint_a);
		acceptor.listen (boost::asio::socket_base::max_listen_connections);
	}
	catch (std::exception const & ex)
	{
		node.logger.always_log ("websocket listen failed: ", ex.what ());
	}
}

void nano::websocket::listener::run ()
{
	if (acceptor.is_open ())
	{
		accept ();
	}
}

void nano::websocket::listener::accept ()
{
	acceptor.async_accept (socket,
	[self_l = shared_from_this ()](boost::system::error_code const & ec) {
		self_l->on_accept (ec);
	});
}

void nano::websocket::listener::on_accept (boost::system::error_code ec)
{
	if (ec)
	{
		node.logger.always_log ("websocket accept failed: ", ec.message ());
	}
	else
	{
		// Create the session and initiate websocket handshake
		auto session (std::make_shared<nano::websocket::session> (*this, std::move (socket)));
		sessions_mutex.lock ();
		sessions.push_back (session);
		sessions_mutex.unlock ();
		session->handshake ();
	}

	if (!stopped)
	{
		accept ();
	}
}

void nano::websocket::listener::broadcast (nano::websocket::message message_a)
{
	std::lock_guard<std::mutex> lk (sessions_mutex);
	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			session_ptr->write (message_a, node);
		}
	}

	// Clean up expired sessions
	sessions.erase (std::remove_if (sessions.begin (), sessions.end (), [](auto & elem) { return elem.expired (); }), sessions.end ());
}

bool nano::websocket::listener::any_subscribers (nano::websocket::topic const & topic_a)
{
	return topic_subscription_count[static_cast<std::size_t> (topic_a)] > 0;
}

void nano::websocket::listener::increase_subscription_count (nano::websocket::topic const & topic_a)
{
	topic_subscription_count[static_cast<std::size_t> (topic_a)] += 1;
}

void nano::websocket::listener::decrease_subscription_count (nano::websocket::topic const & topic_a)
{
	auto & count (topic_subscription_count[static_cast<std::size_t> (topic_a)]);
	release_assert (count > 0);
	count -= 1;
}

nano::websocket::message nano::websocket::message_builder::block_confirmed (std::shared_ptr<nano::block> block_a, nano::account const & account_a, nano::amount const & amount_a, std::string subtype)
{
	nano::websocket::message msg (nano::websocket::topic::confirmation);
	using namespace std::chrono;
	auto milli_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();

	// Common message information
	boost::property_tree::ptree & message_l = msg.contents;
	message_l.add ("topic", from_topic (msg.topic));
	message_l.add ("time", std::to_string (milli_since_epoch));

	// Block confirmation properties
	boost::property_tree::ptree message_node_l;
	message_node_l.add ("account", account_a.to_account ());
	message_node_l.add ("amount", amount_a.to_string_dec ());
	message_node_l.add ("hash", block_a->hash ().to_string ());
	boost::property_tree::ptree block_node_l;
	block_a->serialize_json (block_node_l);
	if (!subtype.empty ())
	{
		block_node_l.add ("subtype", subtype);
	}
	message_node_l.add_child ("block", block_node_l);
	message_l.add_child ("message", message_node_l);

	return msg;
}

std::string nano::websocket::message::to_string ()
{
	std::ostringstream ostream;
	boost::property_tree::write_json (ostream, contents);
	ostream.flush ();
	return ostream.str ();
}
