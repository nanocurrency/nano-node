#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <rai/secure/utility.hpp>
#include <unordered_map>

namespace rai
{
void error_response (std::function<void(boost::property_tree::ptree const &)> response_a, std::string const & message_a);
class node;
/** Configuration options for RPC TLS */
class rpc_secure_config
{
public:
	rpc_secure_config ();
	void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (boost::property_tree::ptree const &);

	/** If true, enable TLS */
	bool enable;
	/** If true, log certificate verification details */
	bool verbose_logging;
	/** Must be set if the private key PEM is password protected */
	std::string server_key_passphrase;
	/** Path to certificate- or chain file. Must be PEM formatted. */
	std::string server_cert_path;
	/** Path to private key file. Must be PEM formatted.*/
	std::string server_key_path;
	/** Path to dhparam file */
	std::string server_dh_path;
	/** Optional path to directory containing client certificates */
	std::string client_certs_path;
};
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
	rpc_secure_config secure;
	uint8_t max_json_depth;
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
	virtual void accept ();
	void stop ();
	void observer_action (rai::account const &);
	boost::asio::ip::tcp::acceptor acceptor;
	std::mutex mutex;
	std::unordered_map<rai::account, std::shared_ptr<rai::payment_observer>> payment_observers;
	rai::rpc_config config;
	rai::node & node;
	bool on;
	static uint16_t const rpc_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7076 : 55000;
};
class rpc_connection : public std::enable_shared_from_this<rai::rpc_connection>
{
public:
	rpc_connection (rai::node &, rai::rpc &);
	virtual void parse_connection ();
	virtual void read ();
	virtual void write_result (std::string body, unsigned version);
	std::shared_ptr<rai::node> node;
	rai::rpc & rpc;
	boost::asio::ip::tcp::socket socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::request<boost::beast::http::string_body> request;
	boost::beast::http::response<boost::beast::http::string_body> res;
	std::atomic_flag responded;
};
class payment_observer : public std::enable_shared_from_this<rai::payment_observer>
{
public:
	payment_observer (std::function<void(boost::property_tree::ptree const &)> const &, rai::rpc &, rai::account const &, rai::amount const &);
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
	std::function<void(boost::property_tree::ptree const &)> response;
	std::atomic_flag completed;
};
/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<rai::rpc> get_rpc (boost::asio::io_service & service_a, rai::node & node_a, rai::rpc_config const & config_a);
}
