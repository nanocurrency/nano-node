#pragma once

#include <rai/utility.hpp>

#include <boost/asio.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <unordered_map>

namespace rai
{
class node;
class rpc_config
{
public:
	rpc_config ();
	rpc_config (bool);
    void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (boost::property_tree::ptree const &);
	boost::asio::ip::address_v6 address;
	uint16_t port;
	bool enable_control;
	uint64_t frontier_request_limit;
	uint64_t chain_request_limit;
};
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
class rpc
{
public:
    rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, rai::node &, rai::rpc_config const &);
    void start ();
    void stop ();
    void operator () (boost::network::http::async_server <rai::rpc>::request const &, boost::network::http::async_server <rai::rpc>::connection_ptr);
    void read_headers (boost::network::http::async_server <rai::rpc>::request const &, std::string const &, boost::network::http::async_server <rai::rpc>::connection_ptr);
	void error_response (boost::network::http::async_server <rai::rpc>::connection_ptr, std::string const &);
    void log (const char *) {}
	std::mutex mutex;
	std::unordered_map <rai::account, std::function <void ()>> payment_observers;
	rai::rpc_config config;
    boost::network::http::async_server <rai::rpc> server;
    rai::node & node;
    bool on;
};
class payment_observer
{
public:
	payment_observer (rai::rpc &, rai::account const &, std::function <void ()> const &);
	~payment_observer ();
	rai::rpc & rpc;
	rai::account account;
};
class rpc_handler
{
public:
	rpc_handler (rai::rpc &, boost::property_tree::ptree const &, boost::network::http::async_server <rai::rpc>::connection_ptr);
	void account_balance ();
	void account_create ();
	void account_list ();
	void account_move ();
	void account_weight ();
	void block ();
	void chain ();
	void frontiers ();
	void keepalive ();
	void password_change ();
	void password_enter ();
	void password_valid ();
	void payment_begin ();
	void payment_check ();
	void payment_end ();
	void payment_wait ();
	void price ();
	void process ();
	void representative ();
	void representative_set ();
	void search_pending ();
	void send ();
	void validate_account_number ();
	void wallet_add ();
	void wallet_contains ();
	void wallet_create ();
	void wallet_destroy ();
	void wallet_export ();
	void wallet_key_valid ();
	void error_response (std::string const &);
	bool decode_unsigned (std::string const &, uint64_t &);
	void send_response (boost::property_tree::ptree &);
	bool payment_wallets (MDB_txn *, rai::uint256_union const &, rai::uint256_union const &, std::shared_ptr <rai::wallet> &, std::shared_ptr <rai::wallet> &);
	rai::rpc & rpc;
	boost::property_tree::ptree const & request;
	boost::network::http::async_server <rai::rpc>::connection_ptr connection;
};
}
