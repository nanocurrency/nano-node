#pragma once

#include <rai/utility.hpp>

#include <beast/http.hpp>

#include <boost/asio.hpp>
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
    rpc (boost::asio::io_service &, rai::node &, rai::rpc_config const &);
    void start ();
    void stop ();
	void observer_action (rai::account const &);
	boost::asio::ip::tcp::acceptor acceptor;
	std::mutex mutex;
	std::unordered_map <rai::account, std::shared_ptr <rai::payment_observer>> payment_observers;
	rai::rpc_config config;
    rai::node & node;
    bool on;
    static uint16_t const rpc_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7076 : 55000;
};
class rpc_connection : public std::enable_shared_from_this <rai::rpc_connection>
{
public:
	rpc_connection (rai::node &, rai::rpc &);
	void parse_connection ();
	std::shared_ptr <rai::node> node;
	rai::rpc & rpc;
	boost::asio::ip::tcp::socket socket;
	beast::streambuf buffer;
	beast::http::request <beast::http::string_body> request;
	beast::http::response <beast::http::string_body> res;
};
class payment_observer : public std::enable_shared_from_this <rai::payment_observer>
{
public:
	payment_observer (std::function <void (boost::property_tree::ptree const &)> const &, rai::rpc &, rai::account const &, rai::amount const &);
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
	std::function <void (boost::property_tree::ptree const &)> response;
	std::atomic_flag completed;
};
class rpc_handler : public std::enable_shared_from_this <rai::rpc_handler>
{
public:
	rpc_handler (rai::node &, rai::rpc &, std::string const &, std::function <void (boost::property_tree::ptree const &)> const &);
	void process_request ();
	void account_balance ();
	void account_block_count ();
	void account_create ();
	void account_get ();
	void account_key ();
	void account_list ();
	void account_move ();
	void account_remove ();
	void account_representative ();
	void account_representative_set ();
	void account_weight ();
	void available_supply ();
	void block ();
	void block_account ();
	void block_count ();
	void bootstrap ();
	void chain ();
	void frontiers ();
	void frontier_count ();
	void frontier_list ();
	void history ();
	void keepalive ();
	void key_create ();
	void key_expand ();
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
	void pending ();
	void process ();
	void rai_to_raw ();
	void rai_from_raw ();
	void representatives ();
	void search_pending ();
	void send ();
	void stop ();
	void validate_account_number ();
	void version ();
	void wallet_add ();
	void wallet_change_seed ();
	void wallet_contains ();
	void wallet_create ();
	void wallet_destroy ();
	void wallet_export ();
	void wallet_key_valid ();
	void wallet_representative ();
	void wallet_representative_set ();
	void work_generate ();
	void work_cancel ();
	std::string body;
	rai::node & node;
	rai::rpc & rpc;
	boost::property_tree::ptree request;
	std::function <void (boost::property_tree::ptree const &)> response;
};
}
