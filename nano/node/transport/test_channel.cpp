#include <nano/node/node.hpp>
#include <nano/node/transport/test_channel.hpp>

nano::transport::test_channel::test_channel (nano::node & node_a) :
	channel (node_a)
{
}

bool nano::transport::test_channel::send_impl (nano::messages::message const & message, nano::transport::traffic_type traffic_type, callback_t callback)
{
	observers.notify (message, traffic_type);

	if (callback)
	{
		callback (boost::system::errc::make_error_code (boost::system::errc::success), message.to_shared_const_buffer ().size ());
	}

	return true;
}