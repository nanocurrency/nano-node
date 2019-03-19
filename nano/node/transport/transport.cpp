#include <nano/node/common.hpp>
#include <nano/node/transport/transport.hpp>

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

nano::endpoint nano::transport::map_endpoint_to_v6 (nano::endpoint const & endpoint_a)
{
	auto endpoint_l (endpoint_a);
	if (endpoint_l.address ().is_v4 ())
	{
		endpoint_l = nano::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
	}
	return endpoint_l;
}

void nano::transport::channel::send_buffer (std::shared_ptr<std::vector<uint8_t>> buffer_a, nano::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	send_buffer_raw (boost::asio::buffer (buffer_a->data (), buffer_a->size ()), callback (buffer_a, detail_a, callback_a));
}

void nano::transport::channel::send (nano::message const & message_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	callback_visitor visitor;
	message_a.visit (visitor);
	auto buffer (message_a.to_bytes ());
	send_buffer (buffer, visitor.result, callback_a);
}
