#pragma once

namespace nano::transport
{
class channel;
class loopback_channel;
class message_filter;
struct peer_info;
class reachout;
class reachout_index;

class tcp_config;
class tcp_channel;
class tcp_channels;
class tcp_listener;
class tcp_server;
class tcp_socket;
}

namespace nano::transport::fake
{
class channel;
}

namespace nano::transport::inproc
{
class channel;
}
