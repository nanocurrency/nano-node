#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_connections.hpp>
#include <nano/node/common.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp.hpp>

#include <boost/format.hpp>

nano::bootstrap_client::bootstrap_client (std::shared_ptr<nano::node> node_a, std::shared_ptr<nano::bootstrap_attempt> attempt_a, std::shared_ptr<nano::transport::channel_tcp> channel_a, std::shared_ptr<nano::socket> socket_a) :
node (node_a),
attempt (attempt_a),
channel (channel_a),
socket (socket_a),
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
start_time (std::chrono::steady_clock::now ())
{
	++attempt->connections;
	receive_buffer->resize (256);
}

nano::bootstrap_client::~bootstrap_client ()
{
	--attempt->connections;
}

double nano::bootstrap_client::block_rate () const
{
	auto elapsed = std::max (elapsed_seconds (), nano::bootstrap_limits::bootstrap_minimum_elapsed_seconds_blockrate);
	return static_cast<double> (block_count.load () / elapsed);
}

double nano::bootstrap_client::elapsed_seconds () const
{
	return std::chrono::duration_cast<std::chrono::duration<double>> (std::chrono::steady_clock::now () - start_time).count ();
}

void nano::bootstrap_client::stop (bool force)
{
	pending_stop = true;
	if (force)
	{
		hard_stop = true;
	}
}
