#include <nano/node/fair_queue.hpp>
#include <nano/node/transport/channel.hpp>

bool nano::fair_queue_traits<std::shared_ptr<nano::transport::channel>>::alive (std::shared_ptr<nano::transport::channel> const & channel)
{
	return !channel || channel->alive ();
}
