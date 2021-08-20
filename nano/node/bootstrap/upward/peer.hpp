#pragma once

#include <memory>

namespace nano
{

namespace transport
{
class channel;
}

namespace bootstrap
{

namespace upward
{

class peer final
{
public:
    explicit peer (std::shared_ptr<nano::transport::channel> connection_a);

    nano::transport::channel & get_connection () const;

private:
    std::shared_ptr<nano::transport::channel> connection;
};

} // namespace upward

} // namespace bootstrap

} // namespace nano
