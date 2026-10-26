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
#include <nano/node/vote_replier.hpp>
#include <nano/node/wallet.hpp>

namespace
{
class process_visitor : public nano::messages::message_visitor
{
public:
	process_visitor (nano::node & node_a, std::shared_ptr<nano::transport::channel> const & channel_a) :
		node{ node_a },
		channel{ channel_a }
	{
	}

	void keepalive (nano::messages::keepalive const & message) override
	{
		// Check for self reported peering port
		auto self_report = message.peers[0];
		if (self_report.address () == boost::asio::ip::address_v6{} && self_report.port () != 0)
		{
			// Remember this for future forwarding to other peers
			nano::endpoint peering_endpoint{ channel->get_remote_endpoint ().address (), self_report.port () };
			channel->set_peering_endpoint (peering_endpoint);
		}
		channel->set_last_keepalive (message);
	}

	void publish (nano::messages::publish const & message) override
	{
		// A peering-only node keeps no ledger and does no block processing
		if (node.flags.peering_only)
		{
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in);
			return;
		}
		// Put blocks that are being initially broadcasted in a separate queue, so that they won't have to compete with rebroadcasted blocks
		// Both queues have the same priority and size, so the potential for exploiting this is limited
		debug_assert (message.digest != 0 || channel->get_type () != nano::transport::transport_type::tcp); // Messages received from the network should have their digest set
		bool added = node.block_processor.add (message.block, message.is_originator () ? nano::block_source::live_originator : nano::block_source::live, channel);
		if (!added)
		{
			node.network.filter.clear (message.digest);
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in);
		}
	}

	void confirm_req (nano::messages::confirm_req const & message) override
	{
		// Don't load nodes with disabled voting
		if (node.is_voting ())
		{
			if (!message.roots_hashes.empty ())
			{
				node.vote_replier.request (message.roots_hashes, channel);
			}
		}
	}

	void confirm_ack (nano::messages::confirm_ack const & message) override
	{
		// A peering-only node keeps no ledger and does no vote processing
		if (node.flags.peering_only)
		{
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_ack, nano::stat::dir::in);
			return;
		}
		// Ignore zero account votes
		if (message.vote->account.is_zero ())
		{
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_ack_zero_account, nano::stat::dir::in);
			return;
		}

		debug_assert (message.digest != 0 || channel->get_type () != nano::transport::transport_type::tcp); // Messages received from the network should have their digest set
		bool added = node.vote_processor.vote (message.vote, channel, message.is_rebroadcasted () ? nano::vote_source::rebroadcast : nano::vote_source::live);
		if (!added)
		{
			node.network.filter.clear (message.digest);
			node.stats.inc (nano::stat::type::message_drop, nano::stat::detail::confirm_ack, nano::stat::dir::in);
		}
	}

	void bulk_pull (nano::messages::bulk_pull const &) override
	{
		debug_assert (false);
	}

	void bulk_pull_account (nano::messages::bulk_pull_account const &) override
	{
		debug_assert (false);
	}

	void bulk_push (nano::messages::bulk_push const &) override
	{
		debug_assert (false);
	}

	void frontier_req (nano::messages::frontier_req const &) override
	{
		debug_assert (false);
	}

	void node_id_handshake (nano::messages::node_id_handshake const & message) override
	{
		node.stats.inc (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	}

	void telemetry_req (nano::messages::telemetry_req const & message) override
	{
		// Ignore telemetry requests as telemetry is being periodically broadcasted since V25+
	}

	void telemetry_ack (nano::messages::telemetry_ack const & message) override
	{
		node.telemetry.process (message, channel);
	}

	void asc_pull_req (nano::messages::asc_pull_req const & message) override
	{
		node.bootstrap_server.request (message, channel);
	}

	void asc_pull_ack (nano::messages::asc_pull_ack const & message) override
	{
		node.bootstrap.process (message, channel);
	}

private:
	nano::node & node;
	std::shared_ptr<nano::transport::channel> channel;
};
}

nano::node_message_dispatcher::node_message_dispatcher (nano::node & node_a) :
	node{ node_a }
{
}

void nano::node_message_dispatcher::dispatch (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const & channel)
{
	debug_assert (message.header.network == node.network_params.network.current_network);
	debug_assert (message.header.version_using >= node.network_params.network.protocol_version_min);

	process_visitor visitor{ node, channel };
	message.visit (visitor);
}
