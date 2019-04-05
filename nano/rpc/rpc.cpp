#include <boost/algorithm/string.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/interface.h>
#include <nano/node/node.hpp>

#include <nano/rpc/rpc.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_handler.hpp>

#ifdef NANO_SECURE_RPC
#include <nano/rpc/rpc_secure.hpp>
#endif

#include <nano/lib/errors.hpp>

nano::rpc::rpc (boost::asio::io_context & io_ctx_a, nano::node & node_a, nano::rpc_config const & config_a) :
acceptor (io_ctx_a),
config (config_a),
node (node_a)
{
}

void nano::rpc::add_block_observer ()
{
	node.observers.blocks.add ([this](std::shared_ptr<nano::block> block_a, nano::account const & account_a, nano::uint128_t const &, bool) {
		observer_action (account_a);
	});
}

void nano::rpc::start (bool rpc_enabled_a)
{
	if (rpc_enabled_a)
	{
		auto endpoint (nano::tcp_endpoint (config.address, config.port));
		acceptor.open (endpoint.protocol ());
		acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

		boost::system::error_code ec;
		acceptor.bind (endpoint, ec);
		if (ec)
		{
			node.logger.always_log (boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ()));
			throw std::runtime_error (ec.message ());
		}

		acceptor.listen ();
	}

	add_block_observer ();

	if (rpc_enabled_a)
	{
		accept ();
	}
}

void nano::rpc::accept ()
{
	auto connection (std::make_shared<nano::rpc_connection> (node, *this));
	acceptor.async_accept (connection->socket, [this, connection](boost::system::error_code const & ec) {
		if (ec != boost::asio::error::operation_aborted && acceptor.is_open ())
		{
			accept ();
		}
		if (!ec)
		{
			connection->parse_connection ();
		}
		else
		{
			this->node.logger.always_log (boost::str (boost::format ("Error accepting RPC connections: %1% (%2%)") % ec.message () % ec.value ()));
		}
	});
}

void nano::rpc::stop ()
{
	acceptor.close ();
}

void nano::rpc::observer_action (nano::account const & account_a)
{
	std::shared_ptr<nano::payment_observer> observer;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			observer = existing->second;
		}
	}
	if (observer != nullptr)
	{
		observer->observe ();
	}
}

nano::payment_observer::payment_observer (std::function<void(boost::property_tree::ptree const &)> const & response_a, nano::rpc & rpc_a, nano::account const & account_a, nano::amount const & amount_a) :
rpc (rpc_a),
account (account_a),
amount (amount_a),
response (response_a)
{
	completed.clear ();
}

void nano::payment_observer::start (uint64_t timeout)
{
	auto this_l (shared_from_this ());
	rpc.node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout), [this_l]() {
		this_l->complete (nano::payment_status::nothing);
	});
}

nano::payment_observer::~payment_observer ()
{
}

void nano::payment_observer::observe ()
{
	if (rpc.node.balance (account) >= amount.number ())
	{
		complete (nano::payment_status::success);
	}
}

void nano::payment_observer::complete (nano::payment_status status)
{
	auto already (completed.test_and_set ());
	if (!already)
	{
		if (rpc.node.config.logging.log_rpc ())
		{
			rpc.node.logger.always_log (boost::str (boost::format ("Exiting payment_observer for account %1% status %2%") % account.to_account () % static_cast<unsigned> (status)));
		}
		switch (status)
		{
			case nano::payment_status::nothing:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("deprecated", "1");
				response_l.put ("status", "nothing");
				response (response_l);
				break;
			}
			case nano::payment_status::success:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("deprecated", "1");
				response_l.put ("status", "success");
				response (response_l);
				break;
			}
			default:
			{
				error_response (response, "Internal payment error");
				break;
			}
		}
		std::lock_guard<std::mutex> lock (rpc.mutex);
		assert (rpc.payment_observers.find (account) != rpc.payment_observers.end ());
		rpc.payment_observers.erase (account);
	}
}

std::unique_ptr<nano::rpc> nano::get_rpc (boost::asio::io_context & io_ctx_a, nano::node & node_a, nano::rpc_config const & config_a)
{
	std::unique_ptr<rpc> impl;

	if (config_a.secure.enable)
	{
#ifdef NANO_SECURE_RPC
		impl.reset (new rpc_secure (io_ctx_a, node_a, config_a));
#else
		std::cerr << "RPC configured for TLS, but the node is not compiled with TLS support" << std::endl;
#endif
	}
	else
	{
		impl.reset (new rpc (io_ctx_a, node_a, config_a));
	}

	return impl;
}
