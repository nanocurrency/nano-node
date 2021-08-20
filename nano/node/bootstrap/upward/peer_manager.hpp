#pragma once

#include <nano/lib/locks.hpp>
#include <nano/node/bootstrap/upward/peer.hpp>

#include <memory>
#include <vector>

namespace nano
{

namespace bootstrap
{

namespace upward
{

class peer_manager final
{
public:
    peer_manager ();

    void add_peer (std::shared_ptr<nano::bootstrap::upward::peer> peer_a);

    nano::bootstrap::upward::peer const & get_best () const;

private:
    mutable nano::mutex mutex;
    std::vector<std::shared_ptr<nano::bootstrap::upward::peer>> peers;
};

} // namespace upward

} // namespace bootstrap

} // namespace nano
