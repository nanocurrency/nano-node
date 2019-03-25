#include <boost/dll/runtime_symbol_info.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>

nano::rpc_secure_config::rpc_secure_config () :
enable (false),
verbose_logging (false)
{
}

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
max_request_size (32 * 1024 * 1024),
io_threads (std::max<unsigned> (4, boost::thread::hardware_concurrency ())),
ipc_port (network_constants.default_ipc_port),
ipc_path ("/tmp/nano"),
num_ipc_connections (network_constants.is_live_network () ? 8 : 1)
{
}

nano::error nano::rpc_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("address", address.to_string ());
	json.put ("port", port);
	json.put ("enable_control", enable_control);
	json.put ("max_json_depth", max_json_depth);
	json.put ("max_request_size", max_request_size);
	json.put ("io_threads", io_threads);
	json.put ("ipc_port", ipc_port);
	json.put ("ipc_path", ipc_path);
	json.put ("num_ipc_connections", num_ipc_connections);
	return json.get_error ();
}

nano::error nano::rpc_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json)
{
	if (!json.empty ())
	{
		auto version_l (json.get_optional<unsigned> ("version"));
		if (!version_l)
		{
			version_l = 1;
			json.put ("version", *version_l);
			json.put ("max_request_size", max_request_size);
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
		json.get_optional<uint64_t> ("max_request_size", max_request_size);
		json.get_optional<unsigned> ("io_threads", io_threads);
		json.get_optional<uint16_t> ("ipc_port", ipc_port);
		json.get_optional<std::string> ("ipc_path", ipc_path);
		json.get_optional<unsigned> ("num_ipc_connections", num_ipc_connections);
	}
	else
	{
		upgraded_a = true;
		serialize_json (json);
	}

	return json.get_error ();
}

namespace nano
{
nano::error read_and_update_rpc_config (boost::filesystem::path const & data_path, nano::rpc_config & config_a)
{
	boost::system::error_code error_chmod;
	nano::jsonconfig json;
	auto config_path = nano::get_rpc_config_path (data_path);
	auto error (json.read_and_update (config_a, config_path));
	nano::set_secure_perm_file (config_path, error_chmod);
	return error;
}

std::string get_default_rpc_filepath ()
{
	boost::system::error_code err;
	auto running_executable_filepath = boost::dll::program_location (err);
	if (err)
	{
		std::cerr << "Failed to find correct RPC path, please set manually in config.json";
		return "";
	}

	// Construct the nano_rpc excutable file path based on where the currently running executable is found.
	auto rpc_filepath = running_executable_filepath.parent_path () / "nano_rpc";
	if (running_executable_filepath.has_extension ())
	{
		rpc_filepath.replace_extension (running_executable_filepath.extension ());
	}

	// Confirm it exists
	if (!boost::filesystem::exists (rpc_filepath))
	{
		std::cerr << "RPC migrated filepath incorrectly, please set manually in config.json";
	}

	return rpc_filepath.string ();
}

}
