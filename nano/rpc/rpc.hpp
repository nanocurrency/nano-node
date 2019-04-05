#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/utility.hpp>
#include <unordered_map>

namespace nano
{
class node;
enum class payment_status
{
	not_a_status,
	unknown,
	nothing, // Timeout and nothing was received
	//insufficient, // Timeout and not enough was received
	//over, // More than requested received
	//success_fork, // Amount received but it involved a fork
	success // Amount received
};
class wallet;
class payment_observer;
class rpc
{
public:
	rpc (boost::asio::io_context &, nano::node &, nano::rpc_config const &);
	virtual ~rpc () = default;

	/**
	 * Start serving RPC requests if \p rpc_enabled_a, otherwise this will only
	 * add a block observer since requests may still arrive via IPC.
	 */
	void start (bool rpc_enabled_a = true);
	void add_block_observer ();
	virtual void accept ();
	void stop ();
	void observer_action (nano::account const &);
	boost::asio::ip::tcp::acceptor acceptor;
	std::mutex mutex;
	std::unordered_map<nano::account, std::shared_ptr<nano::payment_observer>> payment_observers;
	nano::rpc_config config;
	nano::node & node;
	bool on;
};

class payment_observer : public std::enable_shared_from_this<nano::payment_observer>
{
public:
	payment_observer (std::function<void(boost::property_tree::ptree const &)> const &, nano::rpc &, nano::account const &, nano::amount const &);
	~payment_observer ();
	void start (uint64_t);
	void observe ();
	void complete (nano::payment_status);
	std::mutex mutex;
	std::condition_variable condition;
	nano::rpc & rpc;
	nano::account account;
	nano::amount amount;
	std::function<void(boost::property_tree::ptree const &)> response;
	std::atomic_flag completed;
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<nano::rpc> get_rpc (boost::asio::io_context & io_ctx_a, nano::node & node_a, nano::rpc_config const & config_a);
}
