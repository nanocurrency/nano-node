#pragma once

#include <nano/messages/fwd.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/fwd.hpp>

#include <memory>

namespace nano
{
// Hands a message that arrived on a channel to whatever handles its type
class message_dispatcher
{
public:
	virtual ~message_dispatcher () = default;

	virtual void dispatch (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const & channel) = 0;
};

// Routes each message type to the node component that handles it
class node_message_dispatcher final : public message_dispatcher
{
public:
	explicit node_message_dispatcher (nano::node & node);

	void dispatch (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const & channel) override;

private:
	nano::node & node;
};
}
