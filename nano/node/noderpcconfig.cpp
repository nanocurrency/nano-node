#include <nano/node/noderpcconfig.hpp>

#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>

nano::error nano::node_rpc_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("enable_sign_hash", enable_sign_hash);
	json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));
	json.put ("rpc_path", rpc_path);
	json.put ("rpc_in_process", rpc_in_process);
	return json.get_error ();
}

nano::error nano::node_rpc_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json, boost::filesystem::path const & data_path)
{
	auto version_l (json.get_optional<unsigned> ("version"));
	if (!version_l)
	{
		version_l = 1;
		json.put ("version", *version_l);
		json.erase ("frontier_request_limit");
		json.erase ("chain_request_limit");

		// Don't migrate enable_sign_hash as this is not needed by the external rpc process, but first save it.
		json.get_optional ("enable_sign_hash", enable_sign_hash, false);

		json.erase ("enable_sign_hash");
		json.erase ("max_work_generate_difficulty");

		migrate (json, data_path);

		json.erase ("io_threads");

		json.put ("enable_sign_hash", enable_sign_hash);
		json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));
		json.put ("rpc_path", get_default_rpc_filepath ());
		bool rpc_in_process_l;
		json.get_optional<bool> ("rpc_in_process", rpc_in_process_l);
		if (!rpc_in_process)
		{
			json.put ("rpc_in_process", true);
		}

		upgraded_a = true;
	}

	json.get_optional<bool> ("enable_sign_hash", enable_sign_hash);
	std::string max_work_generate_difficulty_text;
	json.get_optional<std::string> ("max_work_generate_difficulty", max_work_generate_difficulty_text);
	if (!max_work_generate_difficulty_text.empty ())
	{
		nano::from_string_hex (max_work_generate_difficulty_text, max_work_generate_difficulty);
	}
	json.get_optional<std::string> ("rpc_path", rpc_path);
	json.get_optional<bool> ("rpc_in_process", rpc_in_process);
	return json.get_error ();
}

void nano::node_rpc_config::migrate (nano::jsonconfig & json, boost::filesystem::path const & data_path)
{
	// Copy RPC config if the file doesn't exist already
	nano::network_constants network_constants;
	auto default_ipc_port = network_constants.is_live_network () ? 7077 : 24077;

	nano::jsonconfig rpc_json;
	auto rpc_config_path = nano::get_rpc_config_path (data_path);
	auto rpc_error (rpc_json.read<nano::rpc_config> (rpc_config_path));
	if (rpc_error || rpc_json.empty ())
	{
		// Migrate RPC info across
		json.write (rpc_config_path);
	}
}
