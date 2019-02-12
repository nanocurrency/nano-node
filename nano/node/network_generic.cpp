#include <cassert>
#include <nano/node/common.hpp>
#include <nano/node/network_generic.hpp>
#include <nano/node/node.hpp>

nano::net::socket_addr nano::net::socket_addr::make_tcp (std::string address_a, bool & error_a)
{
	nano::net::socket_addr endpoint;
	error_a = nano::parse_tcp_endpoint (address_a, endpoint);
	return endpoint;
}

nano::net::socket_addr nano::net::socket_addr::make_udp (std::string address_a, bool & error_a)
{
	nano::net::socket_addr endpoint;
	error_a = nano::parse_udp_endpoint (address_a, endpoint);
	return endpoint;
}

nano::net::socket_addr nano::net::socket_addr::tcp_map_to_v6 (nano::net::socket_addr const & endpoint_a)
{
	auto endpoint_l (endpoint_a);
	if (endpoint_l.address ().is_v4 ())
	{
		endpoint_l = boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
	}
	assert (endpoint_l.address ().is_v6 ());
	return endpoint_l;
}

nano::net::socket_addr nano::net::socket_addr::udp_map_to_v6 (nano::net::socket_addr const & endpoint_a)
{
	auto endpoint_l (endpoint_a);
	if (endpoint_l.address ().is_v4 ())
	{
		endpoint_l = boost::asio::ip::udp::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
	}
	assert (endpoint_l.address ().is_v6 ());
	return endpoint_l;
}

nano::net::socket_addr nano::net::socket_addr::map_to_v6 () const
{
	if (is_udp ())
	{
		return nano::net::socket_addr::udp_map_to_v6 (*this);
	}
	else if (is_tcp ())
	{
		return nano::net::socket_addr::tcp_map_to_v6 (*this);
	}
	else
	{
		assert (false);
		return nano::net::socket_addr ();
	}
}

nano::net::socket_addr nano::net::socket_addr::make_tcp (const boost::asio::ip::address & address_a, unsigned short port_a)
{
	return boost::asio::ip::tcp::endpoint (address_a, port_a);
}

nano::net::socket_addr nano::net::socket_addr::make_udp (const boost::asio::ip::address & address_a, unsigned short port_a)
{
	return boost::asio::ip::udp::endpoint (address_a, port_a);
}

nano::net::socket_addr nano::net::socket_addr::make_default_tcp ()
{
	return boost::asio::ip::tcp::endpoint{};
}

nano::net::socket_addr nano::net::socket_addr::make_default_udp ()
{
	return boost::asio::ip::udp::endpoint{};
}

nano::net::tcp_client::tcp_client (nano::node & node_a) :
cutoff (std::numeric_limits<uint64_t>::max ()),
node (node_a),
socket_m (node_a.io_ctx)
{
}

void nano::net::tcp_client::async_connect (nano::net::socket_addr const & endpoint_a, std::function<void(boost::system::error_code const &)> callback_a)
{
	assert (endpoint_a.is_tcp ());
	checkup ();
	auto this_l (shared_from_this ());
	start ();
	socket_m.async_connect (endpoint_a.tcp (), [this_l, callback_a](boost::system::error_code const & ec) {
		this_l->stop ();
		callback_a (ec);
	});
}

void nano::net::tcp_client::async_read (uint8_t * buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)> callback_a)
{
	auto this_l (shared_from_this ());
	if (socket_m.is_open ())
	{
		start ();
		boost::asio::async_read (socket_m, boost::asio::buffer (buffer_a, size_a), [this_l, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::in, size_a);
			this_l->stop ();
			callback_a (ec, size_a, this_l->socket_m.remote_endpoint ());
		});
	}
}

void nano::net::tcp_client::async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)> callback_a)
{
	assert (size_a <= buffer_a->size ());
	auto this_l (shared_from_this ());
	if (socket_m.is_open ())
	{
		start ();
		boost::asio::async_read (socket_m, boost::asio::buffer (buffer_a->data (), size_a), [this_l, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::in, size_a);
			this_l->stop ();
			boost::system::error_code ec_remote;
			auto remote (this_l->socket_m.remote_endpoint (ec_remote));
			callback_a (ec, size_a, remote);
		});
	}
}

void nano::net::tcp_client::async_write (uint8_t const * buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	auto this_l (shared_from_this ());
	if (socket_m.is_open ())
	{
		start ();
		boost::asio::async_write (socket_m, boost::asio::buffer (buffer_a, size_a), [this_l, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::out, size_a);
			this_l->stop ();
			callback_a (ec, size_a);
		});
	}
}

void nano::net::tcp_client::async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	auto this_l (shared_from_this ());
	if (socket_m.is_open ())
	{
		start ();
		boost::asio::async_write (socket_m, boost::asio::buffer (buffer_a->data (), buffer_a->size ()), [this_l, callback_a, buffer_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic_tcp, nano::stat::dir::out, size_a);
			this_l->stop ();
			callback_a (ec, size_a);
		});
	}
}

void nano::net::tcp_client::start (std::chrono::steady_clock::time_point timeout_a)
{
	cutoff = timeout_a.time_since_epoch ().count ();
}

void nano::net::tcp_client::stop ()
{
	cutoff = std::numeric_limits<uint64_t>::max ();
}

void nano::net::tcp_client::checkup ()
{
	std::weak_ptr<nano::net::tcp_client> this_w (shared_from_this ());
	node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (10), [this_w]() {
		if (auto this_l = this_w.lock ())
		{
			if (this_l->cutoff != std::numeric_limits<uint64_t>::max () && this_l->cutoff < static_cast<uint64_t> (std::chrono::steady_clock::now ().time_since_epoch ().count ()))
			{
				if (this_l->node.config.logging.bulk_pull_logging ())
				{
					boost::system::error_code ec;
					BOOST_LOG (this_l->node.log) << boost::str (boost::format ("Disconnecting from %1% due to timeout") % this_l->remote_endpoint (ec));
				}
				this_l->close ();
			}
			else
			{
				this_l->checkup ();
			}
		}
	});
}

nano::net::udp_client::udp_client (nano::node & node_a, nano::net::socket_addr local_endpoint_a) :
node (node_a),
socket (node_a.io_ctx, local_endpoint_a.udp ())
{
}

void nano::net::udp_client::async_connect (nano::net::socket_addr const & endpoint_a, std::function<void(boost::system::error_code const &)> callback_a)
{
	boost::system::error_code ec;
	remote_endpoint_m = endpoint_a.udp ();
	callback_a (ec);
}

void nano::net::udp_client::async_read (uint8_t * buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)> callback_a)
{
	auto this_l (shared_from_this ());
	if (socket.is_open ())
	{
		auto remote_a (std::make_shared<boost::asio::ip::udp::endpoint> ());
		socket.async_receive_from (boost::asio::buffer (buffer_a, size_a), *remote_a, [this_l, remote_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic, nano::stat::dir::in, size_a);
			callback_a (ec, size_a, *remote_a);
		});
	}
}

void nano::net::udp_client::async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)> callback_a)
{
	assert (size_a <= buffer_a->size ());
	auto this_l (shared_from_this ());
	if (socket.is_open ())
	{
		auto remote_a (std::make_shared<boost::asio::ip::udp::endpoint> ());
		socket.async_receive_from (boost::asio::buffer (buffer_a->data (), size_a), *remote_a, [this_l, remote_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic, nano::stat::dir::in, size_a);
			callback_a (ec, size_a, *remote_a);
		});
	}
}

void nano::net::udp_client::async_write (uint8_t const * buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	auto this_l (shared_from_this ());
	if (socket.is_open ())
	{
		socket.async_send_to (boost::asio::buffer (buffer_a, size_a), remote_endpoint_m, [this_l, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic, nano::stat::dir::out, size_a);
			callback_a (ec, size_a);
		});
	}
}

void nano::net::udp_client::async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a)
{
	auto this_l (shared_from_this ());
	if (socket.is_open ())
	{
		socket.async_send_to (boost::asio::buffer (buffer_a->data (), buffer_a->size ()), remote_endpoint_m, [this_l, callback_a, buffer_a](boost::system::error_code const & ec, size_t size_a) {
			this_l->node.stats.add (nano::stat::type::traffic, nano::stat::dir::out, size_a);
			callback_a (ec, size_a);
		});
	}
}
