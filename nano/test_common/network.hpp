#pragma once

#include <nano/node/common.hpp>

namespace nano
{
class node;
class system;

namespace transport
{
	class channel_tcp;
}

std::shared_ptr<nano::transport::channel_tcp> establish_tcp (nano::system &, nano::node &, nano::endpoint const &);
}
