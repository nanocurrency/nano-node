#pragma once

#include <rai/utility.hpp>

#include <boost/asio.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <atomic>
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
class payment_observer;
class rpc
{
public:
    rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, rai::node &, rai::rpc_config const &);
    void start ();
    void stop ();
    void operator () (boost::network::http::async_server <rai::rpc>::request const &, boost::network::http::async_server <rai::rpc>::connection_ptr);
    void log (const char *) {}
	bool decode_unsigned (std::string const &, uint64_t &);
	void error_response (boost::network::http::async_server <rai::rpc>::connection_ptr, std::string const &);
	void send_response (boost::network::http::async_server <rai::rpc>::connection_ptr, boost::property_tree::ptree &);
	void observer_action (rai::account const &);
	std::mutex mutex;
	std::unordered_map <rai::account, std::shared_ptr <rai::payment_observer>> payment_observers;
	rai::rpc_config config;
    boost::network::http::async_server <rai::rpc> server;
    rai::node & node;
    bool on;
    static uint16_t const rpc_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7076 : 55000;
};
class payment_observer : public std::enable_shared_from_this <rai::payment_observer>
{
public:
	payment_observer (boost::network::http::async_server <rai::rpc>::connection_ptr, rai::rpc &, rai::account const &, rai::amount const &);
	~payment_observer ();
	void start (uint64_t);
	void observe ();
	void timeout ();
	void complete (rai::payment_status);
	std::mutex mutex;
	std::condition_variable condition;
	rai::rpc & rpc;
	rai::account account;
	rai::amount amount;
	boost::network::http::async_server <rai::rpc>::connection_ptr connection;
	std::atomic_flag completed;
};
class rpc_handler : public std::enable_shared_from_this <rai::rpc_handler>
{
public:
	rpc_handler (rai::rpc &, size_t, boost::network::http::async_server <rai::rpc>::connection_ptr);
    void read_or_process ();
	void part_handler (boost::network::http::async_server <rai::rpc>::connection::input_range, boost::system::error_code, size_t);
	void process_request ();
	void account_balance ();
	void account_create ();
	void account_list ();
	void account_move ();
	void account_weight ();
	void available_supply ();
	void block ();
	void block_account ();
	void block_count ();
	void chain ();
	void frontiers ();
	void frontier_count ();
	void history ();
	void keepalive ();
	void krai_to_raw ();
	void krai_from_raw ();
	void mrai_to_raw ();
	void mrai_from_raw ();
	void password_change ();
	void password_enter ();
	void password_valid ();
	void payment_begin ();
	void payment_init ();
	void payment_end ();
	void payment_wait ();
	void peers ();
	void process ();
	void rai_to_raw ();
	void rai_from_raw ();
	void representative ();
	void search_pending ();
	void send ();
	void stop ();
	void validate_account_number ();
	void version ();
	void wallet_add ();
	void wallet_contains ();
	void wallet_create ();
	void wallet_destroy ();
	void wallet_export ();
	void wallet_key_valid ();
	void wallet_representative ();
	void wallet_representative_set ();
	void work_generate ();
	void work_cancel ();
	size_t length;
	std::string body;
	rai::rpc & rpc;
	boost::property_tree::ptree request;
	boost::network::http::async_server <rai::rpc>::connection_ptr connection;
};
}
