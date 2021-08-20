#include <nano/node/bootstrap/upward/peer_manager.hpp>

nano::bootstrap::upward::peer_manager::peer_manager () :
mutex{ },
peers{ }
{
	//
}

void nano::bootstrap::upward::peer_manager::add_peer (std::shared_ptr<nano::bootstrap::upward::peer> peer_a)
{
    nano::lock_guard<nano::mutex> lock{ mutex };

    peers.push_back (std::move (peer_a));
}

nano::bootstrap::upward::peer const & nano::bootstrap::upward::peer_manager::get_best () const
{
    // this decision should be made based on some scoring

    nano::lock_guard<nano::mutex> lock{ mutex };

	return *peers.front ();
}
