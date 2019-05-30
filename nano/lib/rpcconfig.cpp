#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <boost/dll/runtime_symbol_info.hpp>

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
enable_control (enable_control_a)
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

	nano::jsonconfig rpc_process_l;
	rpc_process_l.put ("io_threads", rpc_process.io_threads);
	rpc_process_l.put ("ipc_port", rpc_process.ipc_port);
	rpc_process_l.put ("num_ipc_connections", rpc_process.num_ipc_connections);
	json.put_child ("process", rpc_process_l);
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

			nano::jsonconfig rpc_process_l;
			rpc_process_l.put ("io_threads", rpc_process.io_threads);
			rpc_process_l.put ("ipc_port", rpc_process.ipc_port);
			rpc_process_l.put ("num_ipc_connections", rpc_process.num_ipc_connections);
			json.put_child ("process", rpc_process_l);
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

		auto rpc_process_l (json.get_optional_child ("process"));
		if (rpc_process_l)
		{
			rpc_process_l->get_optional<unsigned> ("io_threads", rpc_process.io_threads);
			rpc_process_l->get_optional<uint16_t> ("ipc_port", rpc_process.ipc_port);
			rpc_process_l->get_optional<unsigned> ("num_ipc_connections", rpc_process.num_ipc_connections);
		}
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

	// Construct the nano_rpc excutable file path based on where the currently running executable is found.
	auto rpc_filepath = running_executable_filepath.parent_path () / "nano_rpc";
	if (running_executable_filepath.has_extension ())
	{
		rpc_filepath.replace_extension (running_executable_filepath.extension ());
	}

	return rpc_filepath.string ();
}
}
