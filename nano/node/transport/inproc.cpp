#include <nano/node/node.hpp>
#include <nano/node/transport/inproc.hpp>

#include <boost/format.hpp>

nano::transport::inproc::channel::channel (nano::node & node, nano::node & destination) :
	transport::channel{ node },
	destination{ destination },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.node_id.pub);
	set_network_version (node.network_params.network.protocol_version);
}

std::size_t nano::transport::inproc::channel::hash_code () const
{
	std::hash<::nano::endpoint> hash;
	return hash (endpoint);
}

bool nano::transport::inproc::channel::operator== (nano::transport::channel const & other_a) const
{
	return endpoint == other_a.get_endpoint ();
}

class message_visitor_inbound : public nano::message_visitor
{
public:
	message_visitor_inbound (decltype (nano::network::inbound) & inbound, std::shared_ptr<nano::transport::inproc::channel> channel) :
		inbound{ inbound },
		channel{ channel }
	{
	}
	decltype (nano::network::inbound) & inbound;
	std::shared_ptr<nano::transport::inproc::channel> channel;
	void keepalive (nano::keepalive const & message)
	{
		inbound (message, channel);
	}
	void publish (nano::publish const & message)
	{
		inbound (message, channel);
	}
	void confirm_req (nano::confirm_req const & message)
	{
		inbound (message, channel);
	}
	void confirm_ack (nano::confirm_ack const & message)
	{
		inbound (message, channel);
	}
	void bulk_pull (nano::bulk_pull const & message)
	{
		inbound (message, channel);
	}
	void bulk_pull_account (nano::bulk_pull_account const & message)
	{
		inbound (message, channel);
	}
	void bulk_push (nano::bulk_push const & message)
	{
		inbound (message, channel);
	}
	void frontier_req (nano::frontier_req const & message)
	{
		inbound (message, channel);
	}
	void node_id_handshake (nano::node_id_handshake const & message)
	{
		inbound (message, channel);
	}
	void telemetry_req (nano::telemetry_req const & message)
	{
		inbound (message, channel);
	}
	void telemetry_ack (nano::telemetry_ack const & message)
	{
		inbound (message, channel);
	}
};

void nano::transport::inproc::channel::send_buffer (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::buffer_drop_policy drop_policy_a)
{
	auto remote_channel = std::make_shared<nano::transport::inproc::channel> (destination, node);
	message_visitor_inbound visitor{ destination.network.inbound, remote_channel };
	nano::message_parser parser{ destination.network.publish_filter, destination.block_uniquer, destination.vote_uniquer, visitor, destination.work, destination.network_params.network };
	auto bytes = buffer_a.to_bytes ();
	parser.deserialize_buffer (bytes.data (), bytes.size ());
}

std::string nano::transport::inproc::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
