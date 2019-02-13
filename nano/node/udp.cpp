#include <nano/node/udp.hpp>

namespace
{
class callback_visitor : public nano::message_visitor
{
public:
	void keepalive (nano::keepalive const & message_a) override
	{
		result = nano::stat::detail::keepalive;
	}
	void publish (nano::publish const & message_a) override
	{
		result = nano::stat::detail::publish;
	}
	void confirm_req (nano::confirm_req const & message_a) override
	{
		result = nano::stat::detail::confirm_req;
	}
	void confirm_ack (nano::confirm_ack const & message_a) override
	{
		result = nano::stat::detail::confirm_ack;
	}
	void bulk_pull (nano::bulk_pull const & message_a) override
	{
		result = nano::stat::detail::bulk_pull;
	}
	void bulk_pull_account (nano::bulk_pull_account const & message_a) override
	{
		result = nano::stat::detail::bulk_pull_account;
	}
	void bulk_push (nano::bulk_push const & message_a) override
	{
		result = nano::stat::detail::bulk_push;
	}
	void frontier_req (nano::frontier_req const & message_a) override
	{
		result = nano::stat::detail::frontier_req;
	}
	void node_id_handshake (nano::node_id_handshake const & message_a) override
	{
		result = nano::stat::detail::node_id_handshake;
	}
	nano::stat::detail result;
};
}

nano::message_sink_udp::message_sink_udp (nano::node & node_a, nano::endpoint const & endpoint_a) :
node (node_a),
endpoint (endpoint_a)
{
	assert (endpoint_a.address ().is_v6 ());
}

void nano::message_sink_udp::send_buffer (uint8_t const * data_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) const
{
	node.network.socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint, callback_a);
}

void nano::message_sink_udp::sink (nano::message const & message_a) const
{
	callback_visitor visitor;
	message_a.visit (visitor);
	auto buffer (message_a.to_bytes ());
	send_buffer (buffer->data (), buffer->size (), callback (buffer, visitor.result));
}

std::function<void(boost::system::error_code const &, size_t)> nano::message_sink_udp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a) const
{
	return [ buffer_a, node = std::weak_ptr<nano::node> (node.shared ()), detail_a ](boost::system::error_code const & ec, size_t size_a)
	{
		if (auto node_l = node.lock ())
		{
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (nano::stat::type::error, nano::stat::detail::unreachable_host, nano::stat::dir::out);
			}
			if (!ec)
			{
				node_l->stats.add (nano::stat::type::traffic, nano::stat::dir::out, size_a);
				node_l->stats.inc (nano::stat::type::message, detail_a, nano::stat::dir::out);
			}
		}
	};
}
