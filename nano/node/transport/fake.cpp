#include <nano/node/node.hpp>
#include <nano/node/transport/fake.hpp>

#include <boost/format.hpp>

nano::transport::fake::channel::channel (nano::node & node) :
	transport::channel{ node },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.node_id.pub);
	set_network_version (node.network_params.network.protocol_version);
}

/**
 * The send function behaves like a null device, it throws the data away and returns success.
 */
bool nano::transport::fake::channel::send_buffer (nano::shared_const_buffer const & buffer, nano::transport::traffic_type traffic_type, nano::transport::channel::callback_t callback)
{
	auto size = buffer.size ();
	if (callback)
	{
		node.io_ctx.post ([callback, size] () {
			callback (boost::system::errc::make_error_code (boost::system::errc::success), size);
		});
	}
	return true;
}

std::string nano::transport::fake::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}