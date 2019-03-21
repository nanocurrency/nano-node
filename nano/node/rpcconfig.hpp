#pragma once

#include <boost/asio.hpp>
#include <nano/lib/errors.hpp>
#include <string>

namespace nano
{
class jsonconfig;

/** Configuration options for RPC TLS */
class rpc_secure_config
{
public:
	rpc_secure_config ();
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (nano::jsonconfig &);

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
	rpc_config (bool = false);
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool & upgraded_a, nano::jsonconfig &);
	nano::network_params network_params;
	boost::asio::ip::address_v6 address;
	uint16_t port;
	bool enable_control;
	rpc_secure_config secure;
	uint8_t max_json_depth;
	bool enable_sign_hash;
	uint64_t max_request_size;
	uint64_t max_work_generate_difficulty;
	static int json_version ()
	{
		return 1;
	}
};
}
