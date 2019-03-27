#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>

nano::error nano::rpc_secure_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("enable", enable);
	json.put ("verbose_logging", verbose_logging);
	json.put ("server_key_passphrase", server_key_passphrase);
	json.put ("server_cert_path", server_cert_path);
	json.put ("server_key_path", server_key_path);
	json.put ("server_dh_path", server_dh_path);
	json.put ("client_certs_path", client_certs_path);
	return json.get_error ();
}

nano::error nano::rpc_secure_config::deserialize_json (nano::jsonconfig & json)
{
	json.get_required<bool> ("enable", enable);
	json.get_required<bool> ("verbose_logging", verbose_logging);
	json.get_required<std::string> ("server_key_passphrase", server_key_passphrase);
	json.get_required<std::string> ("server_cert_path", server_cert_path);
	json.get_required<std::string> ("server_key_path", server_key_path);
	json.get_required<std::string> ("server_dh_path", server_dh_path);
	json.get_required<std::string> ("client_certs_path", client_certs_path);
	return json.get_error ();
}

nano::rpc_config::rpc_config (bool enable_control_a) :
address (boost::asio::ip::address_v6::loopback ()),
port (network_constants.default_rpc_port),
enable_control (enable_control_a),
max_json_depth (20),
enable_sign_hash (false),
max_request_size (32 * 1024 * 1024),
max_work_generate_difficulty (0xffffffffc0000000)
{
}

nano::error nano::rpc_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("address", address.to_string ());
	json.put ("port", port);
	json.put ("enable_control", enable_control);
	json.put ("max_json_depth", max_json_depth);
	json.put ("enable_sign_hash", enable_sign_hash);
	json.put ("max_request_size", max_request_size);
	json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));
	return json.get_error ();
}

nano::error nano::rpc_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json)
{
	auto version_l (json.get_optional<unsigned> ("version"));
	if (!version_l)
	{
		version_l = 1;
		json.put ("version", *version_l);
		json.put ("max_request_size", max_request_size);
		json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));
		json.erase ("frontier_request_limit");
		json.erase ("chain_request_limit");

		upgraded_a = true;
	}

	auto rpc_secure_l (json.get_optional_child ("secure"));
	if (rpc_secure_l)
	{
		secure.deserialize_json (*rpc_secure_l);
	}

	json.get_required<boost::asio::ip::address_v6> ("address", address);
	json.get_optional<uint16_t> ("port", port);
	json.get_optional<bool> ("enable_control", enable_control);
	json.get_optional<uint8_t> ("max_json_depth", max_json_depth);
	json.get_optional<bool> ("enable_sign_hash", enable_sign_hash);
	json.get_optional<uint64_t> ("max_request_size", max_request_size);
	std::string max_work_generate_difficulty_text;
	json.get_optional<std::string> ("max_work_generate_difficulty", max_work_generate_difficulty_text);
	if (!max_work_generate_difficulty_text.empty ())
	{
		nano::from_string_hex (max_work_generate_difficulty_text, max_work_generate_difficulty);
	}
	return json.get_error ();
}
