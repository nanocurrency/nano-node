#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include <string>

namespace nano
{
class jsonconfig;

/** Configuration options for RPC TLS */
class rpc_secure_config final
{
public:
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (nano::jsonconfig &);

	/** If true, enable TLS */
	bool enable{ false };
	/** If true, log certificate verification details */
	bool verbose_logging{ false };
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

class rpc_process_config final
{
public:
	nano::network_constants network_constants;
	unsigned io_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	uint16_t ipc_port{ network_constants.default_ipc_port };
	unsigned num_ipc_connections{ network_constants.is_live_network () ? 8u : network_constants.is_beta_network () ? 4u : 1u };
};

class rpc_config final
{
public:
	explicit rpc_config (bool = false);
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool & upgraded_a, nano::jsonconfig &);

	nano::rpc_process_config rpc_process;
	boost::asio::ip::address_v6 address{ boost::asio::ip::address_v6::loopback () };
	uint16_t port{ rpc_process.network_constants.default_rpc_port };
	bool enable_control;
	rpc_secure_config secure;
	uint8_t max_json_depth{ 20 };
	uint64_t max_request_size{ 32 * 1024 * 1024 };
	static int json_version ()
	{
		return 1;
	}
};

nano::error read_and_update_rpc_config (boost::filesystem::path const & data_path, nano::rpc_config & config_a);

std::string get_default_rpc_filepath ();
}
