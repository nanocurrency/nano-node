#include <nano/boost/asio/post.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/secure/network_params.hpp>

#include <boost/format.hpp>

nano::transport::fake::channel::channel (nano::node & node) :
	transport::channel{ node },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.get_node_id ());
	set_network_version (node.network_params.network.protocol_version);
}

/**
 * The send function behaves like a null device, it throws the data away and returns success.
 */
bool nano::transport::fake::channel::send_impl (nano::messages::message const & message, nano::transport::traffic_type traffic_type, nano::transport::channel::callback_t callback)
{
	auto buffer = message.to_shared_const_buffer ();
	auto size = buffer.size ();
	if (callback)
	{
		boost::asio::post (node.io_ctx, [callback, size] () {
			callback (boost::system::errc::make_error_code (boost::system::errc::success), size);
		});
	}
	return true;
}

std::string nano::transport::fake::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
