#pragma once

namespace nano
{

class network_constants;

namespace transport
{
class channel;
}

namespace bootstrap
{

namespace upward
{

class pull_info;

class pull_client final
{
public:
    pull_client (nano::transport::channel & connection_a, nano::network_constants const & network_constants_a);

    void pull (nano::bootstrap::upward::pull_info const & pull_info_a);

private:
    nano::transport::channel & connection;
    nano::network_constants const & network_constants;
};

} // namespace upward

} // namespace bootstrap

} // namespace nano
