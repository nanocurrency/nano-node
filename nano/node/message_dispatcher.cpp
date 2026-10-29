#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/message_dispatcher.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/node/vote_replier.hpp>
#include <nano/node/wallet.hpp>

namespace
{
/*
 * Routes one message to the node component that handles its type.
 * A message that goes nowhere, because no component takes its type, the node is not in a state to use it, or a component refuses it, is counted under `message_drop` by its type.
 */
class dispatch_visitor final : public nano::messages::message_visitor
{
public:
	dispatch_visitor (nano::node & node, std::shared_ptr<nano::transport::channel> const & channel);

	void keepalive (nano::messages::keepalive const & message) override;
	void publish (nano::messages::publish const & message) override;
	void confirm_req (nano::messages::confirm_req const & message) override;
	void confirm_ack (nano::messages::confirm_ack const & message) override;
	void telemetry_ack (nano::messages::telemetry_ack const & message) override;
	void asc_pull_req (nano::messages::asc_pull_req const & message) override;
	void asc_pull_ack (nano::messages::asc_pull_ack const & message) override;

	void vote_relay_req (nano::messages::vote_relay_req const & message) override
	{
		// The relay counts the reason it did not take the request
		if (!node.vote_relay.request (message, channel))
		{
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::vote_relay_req, nano::stat::dir::in);
		}
	}

	void vote_relay_ack (nano::messages::vote_relay_ack const & message) override
	{
		// An ack that matches no request this node made is dropped
		if (!node.vote_relay_client.process (message, channel))
		{
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::vote_relay_ack, nano::stat::dir::in);
		}
	}

	// Every other type is dropped, telemetry requests among them since telemetry has been broadcast since V25
	void default_handler (nano::messages::message const & message) override;

private:
	nano::node & node;
	std::shared_ptr<nano::transport::channel> const & channel; // The dispatcher's argument, which outlives the visitor
};

dispatch_visitor::dispatch_visitor (nano::node & node_a, std::shared_ptr<nano::transport::channel> const & channel_a) :
	node{ node_a },
	channel{ channel_a }
{
}

void dispatch_visitor::keepalive (nano::messages::keepalive const & message)
{
	// The first slot self-reports the port the peer listens on, which is what other peers get told
	auto const & self_report = message.peers[0];
	if (self_report.address ().is_unspecified () && self_report.port () != 0)
	{
		channel->set_peering_endpoint ({ channel->get_remote_endpoint ().address (), self_report.port () });
	}
	channel->set_last_keepalive (message);
}

void dispatch_visitor::publish (nano::messages::publish const & message)
{
	// A peering-only node keeps no ledger and does no block processing
	if (node.flags.peering_only)
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in);
		return;
	}

	debug_assert (message.digest != 0 || channel->get_type () != nano::transport::transport_type::tcp); // A message from the network carries its digest

	// Blocks broadcast by their originator queue apart from rebroadcasts
	auto const source = message.is_originator () ? nano::block_source::live_originator : nano::block_source::live;
	if (!node.block_processor.add (message.block, source, channel))
	{
		// Refused, so the block leaves the duplicate filter for a resend to get another chance
		node.network.filter.clear (message.digest);
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in);
	}
}

void dispatch_visitor::confirm_req (nano::messages::confirm_req const & message)
{
	// A node that does not vote has no reply to give
	if (!node.is_voting ())
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_req_not_voting, nano::stat::dir::in);
		return;
	}

	// A request without roots asks for nothing
	if (message.roots_hashes.empty ())
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_req_empty, nano::stat::dir::in);
		return;
	}

	if (!node.vote_replier.request (message.roots_hashes, channel))
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::in);
	}
}

void dispatch_visitor::confirm_ack (nano::messages::confirm_ack const & message)
{
	// A peering-only node keeps no ledger and does no vote processing
	if (node.flags.peering_only)
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_ack, nano::stat::dir::in);
		return;
	}

	// Nobody holds the zero account, so a vote in its name is dropped
	if (message.vote->account.is_zero ())
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_ack_zero_account, nano::stat::dir::in);
		return;
	}

	debug_assert (message.digest != 0 || channel->get_type () != nano::transport::transport_type::tcp); // A message from the network carries its digest

	auto const source = message.is_rebroadcasted () ? nano::vote_source::rebroadcast : nano::vote_source::live;
	if (!node.vote_processor.vote (message.vote, channel, source))
	{
		// Refused, so the vote leaves the duplicate filter for a resend to get another chance
		node.network.filter.clear (message.digest);
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_ack, nano::stat::dir::in);
	}
}

void dispatch_visitor::telemetry_ack (nano::messages::telemetry_ack const & message)
{
	if (!node.telemetry.process (message, channel))
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::telemetry_ack, nano::stat::dir::in);
	}
}

void dispatch_visitor::asc_pull_req (nano::messages::asc_pull_req const & message)
{
	if (!node.bootstrap_server.request (message, channel))
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::asc_pull_req, nano::stat::dir::in);
	}
}

void dispatch_visitor::asc_pull_ack (nano::messages::asc_pull_ack const & message)
{
	if (!node.bootstrap.process (message, channel))
	{
		node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::asc_pull_ack, nano::stat::dir::in);
	}
}

void dispatch_visitor::default_handler (nano::messages::message const & message)
{
	node.stats.inc (nano::stat::type::message_drop, to_stat_detail (message.type ()), nano::stat::dir::in);
}
}

nano::node_message_dispatcher::node_message_dispatcher (nano::node & node_a) :
	node{ node_a }
{
}

void nano::node_message_dispatcher::dispatch (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	debug_assert (message.header.network == node.network_params.network.current_network);
	debug_assert (message.header.version_using >= node.network_params.network.protocol_version_min);

	node.stats.inc (nano::stat::type::message, to_stat_detail (message.type ()), nano::stat::dir::in);
	node.logger.trace (nano::log::type::message, to_log_detail (message.type ()), nano::log::arg{ "message", message });

	dispatch_visitor visitor{ node, channel };
	message.visit (visitor);
}
