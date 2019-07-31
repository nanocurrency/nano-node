#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/node/node_rpc_config.hpp>

nano::error nano::node_rpc_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("enable_sign_hash", enable_sign_hash);
	json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));

	nano::jsonconfig child_process_l;
	child_process_l.put ("enable", child_process.enable);
	child_process_l.put ("rpc_path", child_process.rpc_path);
	json.put_child ("child_process", child_process_l);
	return json.get_error ();
}

nano::error nano::node_rpc_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json, boost::filesystem::path const & data_path)
{
	auto version_l (json.get_optional<unsigned> ("version"));
	if (!version_l)
	{
		json.erase ("frontier_request_limit");
		json.erase ("chain_request_limit");

		// Don't migrate enable_sign_hash as this is not needed by the external rpc process, but first save it.
		json.get_optional ("enable_sign_hash", enable_sign_hash, false);

		json.erase ("enable_sign_hash");
		json.erase ("max_work_generate_difficulty");

		migrate (json, data_path);

		json.put ("enable_sign_hash", enable_sign_hash);
		json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));

		// Remove options no longer needed after migration
		json.erase ("enable_control");
		json.erase ("address");
		json.erase ("port");
		json.erase ("max_json_depth");
		json.erase ("max_request_size");

		version_l = 1;
		json.put ("version", *version_l);

		nano::jsonconfig child_process_l;
		child_process_l.put ("enable", child_process.enable);
		child_process_l.put ("rpc_path", child_process.rpc_path);
		json.put_child ("child_process", child_process_l);
		upgraded_a = true;
	}

	json.get_optional<bool> ("enable_sign_hash", enable_sign_hash);
	std::string max_work_generate_difficulty_text;
	json.get_optional<std::string> ("max_work_generate_difficulty", max_work_generate_difficulty_text);
	if (!max_work_generate_difficulty_text.empty ())
	{
		nano::from_string_hex (max_work_generate_difficulty_text, max_work_generate_difficulty);
	}

	auto child_process_l (json.get_optional_child ("child_process"));
	if (child_process_l)
	{
		child_process_l->get_optional<bool> ("enable", child_process.enable);
		child_process_l->get_optional<std::string> ("rpc_path", child_process.rpc_path);
	}

	return json.get_error ();
}

void nano::node_rpc_config::migrate (nano::jsonconfig & json, boost::filesystem::path const & data_path)
{
	nano::jsonconfig rpc_json;
	auto rpc_config_path = nano::get_rpc_config_path (data_path);
	auto rpc_error (rpc_json.read (rpc_config_path));
	if (rpc_error || rpc_json.empty ())
	{
		// Migrate RPC info across
		json.write (rpc_config_path);
	}
}
