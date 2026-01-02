#pragma once

#include <nano/messages/asc_pull.hpp>
#include <nano/messages/bulk_pull.hpp>
#include <nano/messages/bulk_pull_account.hpp>
#include <nano/messages/bulk_push.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/frontier_req.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/telemetry.hpp>

namespace nano::messages
{
class message_visitor
{
public:
	virtual ~message_visitor () = default;

	virtual void keepalive (keepalive const & message)
	{
		default_handler (message);
	};
	virtual void publish (publish const & message)
	{
		default_handler (message);
	}
	virtual void confirm_req (confirm_req const & message)
	{
		default_handler (message);
	}
	virtual void confirm_ack (confirm_ack const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull (bulk_pull const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull_account (bulk_pull_account const & message)
	{
		default_handler (message);
	}
	virtual void bulk_push (bulk_push const & message)
	{
		default_handler (message);
	}
	virtual void frontier_req (frontier_req const & message)
	{
		default_handler (message);
	}
	virtual void node_id_handshake (node_id_handshake const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_req (telemetry_req const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_ack (telemetry_ack const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_req (asc_pull_req const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_ack (asc_pull_ack const & message)
	{
		default_handler (message);
	}
	virtual void default_handler (message const &){};
};
}
