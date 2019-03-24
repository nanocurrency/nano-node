#include <nano/node/ipcconfig.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/blocks.hpp>

nano::error nano::ipc::ipc_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("version", json_version ());
	nano::jsonconfig tcp_l;
	// Only write out experimental config values if they're previously set explicitly in the config file
	if (transport_tcp.io_threads >= 0)
	{
		tcp_l.put ("io_threads", transport_tcp.io_threads);
	}
	tcp_l.put ("enable", transport_tcp.enabled);
	tcp_l.put ("port", transport_tcp.port);
	tcp_l.put ("io_timeout", transport_tcp.io_timeout);
	json.put_child ("tcp", tcp_l);

	nano::jsonconfig domain_l;
	if (transport_domain.io_threads >= 0)
	{
		domain_l.put ("io_threads", transport_domain.io_threads);
	}
	domain_l.put ("enable", transport_domain.enabled);
	domain_l.put ("path", transport_domain.path);
	domain_l.put ("io_timeout", transport_domain.io_timeout);
	json.put_child ("local", domain_l);
	json.put ("enable_sign_hash", enable_sign_hash);
	json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));
	return json.get_error ();
}

nano::error nano::ipc::ipc_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json, bool rpc_enable)
{
	try
	{
		auto version_l (json.get_optional<unsigned> ("version"));
		if (!version_l)
		{
			version_l = 1;
			json.put ("version", *version_l);
			json.put ("enable_sign_hash", enable_sign_hash);
			json.put ("max_work_generate_difficulty", nano::to_string_hex (max_work_generate_difficulty));

			// IPC needs to be enabled as it is used as the RPC mechanism, so if RPC is enabled, and IPC is not
			// (it's the default), then force IPC to be enabled.
			if (!transport_tcp.enabled && !transport_domain.enabled)
			{
				auto tcp_l (json.get_optional_child ("tcp"));
				if (tcp_l)
				{
					tcp_l->put ("enable", true);				
				}
				else
				{
					nano::jsonconfig tcp_l;
					if (transport_tcp.io_threads >= 0)
					{
						tcp_l.put ("io_threads", transport_tcp.io_threads);
					}
					tcp_l.put ("enable", transport_tcp.enabled);
					tcp_l.put ("port", transport_tcp.port);
					tcp_l.put ("io_timeout", transport_tcp.io_timeout);
					json.put_child ("tcp", tcp_l);			
				}
			}

			upgraded_a = true;
		}

		auto tcp_l (json.get_optional_child ("tcp"));
		if (tcp_l)
		{
			tcp_l->get_optional<long> ("io_threads", transport_tcp.io_threads, -1);
			tcp_l->get<bool> ("enable", transport_tcp.enabled);
			tcp_l->get<uint16_t> ("port", transport_tcp.port);
			tcp_l->get<size_t> ("io_timeout", transport_tcp.io_timeout);
		}

		auto domain_l (json.get_optional_child ("local"));
		if (domain_l)
		{
			domain_l->get_optional<long> ("io_threads", transport_domain.io_threads, -1);
			domain_l->get<bool> ("enable", transport_domain.enabled);
			domain_l->get<std::string> ("path", transport_domain.path);
			domain_l->get<size_t> ("io_timeout", transport_domain.io_timeout);
		}

		json.get_optional<bool> ("enable_sign_hash", enable_sign_hash);
		std::string max_work_generate_difficulty_text;
		json.get_optional<std::string> ("max_work_generate_difficulty", max_work_generate_difficulty_text);
		if (!max_work_generate_difficulty_text.empty ())
		{
			nano::from_string_hex (max_work_generate_difficulty_text, max_work_generate_difficulty);
		}
	}
	catch (std::runtime_error const & ex)
	{
		json.get_error ().set (ex.what ());
	}
	return json.get_error ();
}
