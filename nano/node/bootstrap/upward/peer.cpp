#include <nano/node/bootstrap/upward/peer.hpp>

#include <utility>

nano::bootstrap::upward::peer::peer (std::shared_ptr<nano::transport::channel> connection_a) :
connection{ std::move(connection_a) }
{
	//
}

nano::transport::channel & nano::bootstrap::upward::peer::get_connection () const
{
    return *connection;
}
