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
void nano::transport::fake::channel::send_buffer (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::transport::buffer_drop_policy drop_policy_a, nano::transport::traffic_type traffic_type)
{
	// auto bytes = buffer_a.to_bytes ();
	auto size = buffer_a.size ();
	if (callback_a)
	{
		node.background ([callback_a, size] () {
			callback_a (boost::system::errc::make_error_code (boost::system::errc::success), size);
		});
	}
}

std::string nano::transport::fake::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}