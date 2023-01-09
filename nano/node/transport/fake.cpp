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
void nano::transport::fake::channel::send_buffer (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a, nano::buffer_drop_policy drop_policy_a)
{
	//auto bytes = buffer_a.to_bytes ();
	auto size = buffer_a.size ();
	if (callback_a)
	{
		node.background ([callback_a, size] () {
			callback_a (boost::system::errc::make_error_code (boost::system::errc::success), size);
		});
	}
}

std::size_t nano::transport::fake::channel::hash_code () const
{
	std::hash<::nano::endpoint> hash;
	return hash (endpoint);
}

bool nano::transport::fake::channel::operator== (nano::transport::channel const & other_a) const
{
	return endpoint == other_a.get_endpoint ();
}

bool nano::transport::fake::channel::operator== (nano::transport::fake::channel const & other_a) const
{
	return endpoint == other_a.get_endpoint ();
}

std::string nano::transport::fake::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}