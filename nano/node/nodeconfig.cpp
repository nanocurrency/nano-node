#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/node/nodeconfig.hpp>
// NOTE: to reduce compile times, this include can be replaced by more narrow includes
// once nano::network is factored out of node.{c|h}pp
#include <nano/node/node.hpp>

namespace
{
const char * preconfigured_peers_key = "preconfigured_peers";
const char * signature_checker_threads_key = "signature_checker_threads";
const char * pow_sleep_interval_key = "pow_sleep_interval";
const char * default_beta_peer_network = "peering-beta.nano.org";
const char * default_live_peer_network = "peering.nano.org";
}

nano::node_config::node_config () :
node_config (0, nano::logging ())
{
}

nano::node_config::node_config (uint16_t peering_port_a, nano::logging const & logging_a) :
peering_port (peering_port_a),
logging (logging_a)
{
	// The default constructor passes 0 to indicate we should use the default port,
	// which is determined at node startup based on active network.
	if (peering_port == 0)
	{
		peering_port = network_params.network.default_node_port;
	}
	const char * epoch_message ("epoch v1 block");
	strncpy ((char *)epoch_block_link.bytes.data (), epoch_message, epoch_block_link.bytes.size ());
	epoch_block_signer = network_params.ledger.genesis_account;
	switch (network_params.network.network ())
	{
		case nano::nano_networks::nano_test_network:
			enable_voting = true;
			preconfigured_representatives.push_back (network_params.ledger.genesis_account);
			break;
		case nano::nano_networks::nano_beta_network:
			preconfigured_peers.push_back (default_beta_peer_network);
			preconfigured_representatives.emplace_back ("A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F");
			preconfigured_representatives.emplace_back ("259A4011E6CAD1069A97C02C3C1F2AAA32BC093C8D82EE1334F937A4BE803071");
			preconfigured_representatives.emplace_back ("259A40656144FAA16D2A8516F7BE9C74A63C6CA399960EDB747D144ABB0F7ABD");
			preconfigured_representatives.emplace_back ("259A40A92FA42E2240805DE8618EC4627F0BA41937160B4CFF7F5335FD1933DF");
			preconfigured_representatives.emplace_back ("259A40FF3262E273EC451E873C4CDF8513330425B38860D882A16BCC74DA9B73");
			break;
		case nano::nano_networks::nano_live_network:
			preconfigured_peers.push_back (default_live_peer_network);
			preconfigured_representatives.emplace_back ("A30E0A32ED41C8607AA9212843392E853FCBCB4E7CB194E35C94F07F91DE59EF");
			preconfigured_representatives.emplace_back ("67556D31DDFC2A440BF6147501449B4CB9572278D034EE686A6BEE29851681DF");
			preconfigured_representatives.emplace_back ("5C2FBB148E006A8E8BA7A75DD86C9FE00C83F5FFDBFD76EAA09531071436B6AF");
			preconfigured_representatives.emplace_back ("AE7AC63990DAAAF2A69BF11C913B928844BF5012355456F2F164166464024B29");
			preconfigured_representatives.emplace_back ("BD6267D6ECD8038327D2BCC0850BDF8F56EC0414912207E81BCF90DFAC8A4AAA");
			preconfigured_representatives.emplace_back ("2399A083C600AA0572F5E36247D978FCFC840405F8D4B6D33161C0066A55F431");
			preconfigured_representatives.emplace_back ("2298FAB7C61058E77EA554CB93EDEEDA0692CBFCC540AB213B2836B29029E23A");
			preconfigured_representatives.emplace_back ("3FE80B4BC842E82C1C18ABFEEC47EA989E63953BC82AC411F304D13833D52A56");
			break;
		default:
			assert (false);
			break;
	}
}

nano::error nano::node_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("peering_port", peering_port);
	json.put ("bootstrap_fraction_numerator", bootstrap_fraction_numerator);
	json.put ("receive_minimum", receive_minimum.to_string_dec ());

	nano::jsonconfig logging_l;
	logging.serialize_json (logging_l);
	json.put_child ("logging", logging_l);

	nano::jsonconfig work_peers_l;
	for (auto i (work_peers.begin ()), n (work_peers.end ()); i != n; ++i)
	{
		work_peers_l.push (boost::str (boost::format ("%1%:%2%") % i->first % i->second));
	}
	json.put_child ("work_peers", work_peers_l);
	nano::jsonconfig preconfigured_peers_l;
	for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
	{
		preconfigured_peers_l.push (*i);
	}
	json.put_child (preconfigured_peers_key, preconfigured_peers_l);

	nano::jsonconfig preconfigured_representatives_l;
	for (auto i (preconfigured_representatives.begin ()), n (preconfigured_representatives.end ()); i != n; ++i)
	{
		preconfigured_representatives_l.push (i->to_account ());
	}
	json.put_child ("preconfigured_representatives", preconfigured_representatives_l);

	json.put ("online_weight_minimum", online_weight_minimum.to_string_dec ());
	json.put ("online_weight_quorum", online_weight_quorum);
	json.put ("password_fanout", password_fanout);
	json.put ("io_threads", io_threads);
	json.put ("network_threads", network_threads);
	json.put ("work_threads", work_threads);
	json.put (signature_checker_threads_key, signature_checker_threads);
	json.put ("enable_voting", enable_voting);
	json.put ("bootstrap_connections", bootstrap_connections);
	json.put ("bootstrap_connections_max", bootstrap_connections_max);
	json.put ("callback_address", callback_address);
	json.put ("callback_port", callback_port);
	json.put ("callback_target", callback_target);
	json.put ("lmdb_max_dbs", lmdb_max_dbs);
	json.put ("block_processor_batch_max_time", block_processor_batch_max_time.count ());
	json.put ("allow_local_peers", allow_local_peers);
	json.put ("vote_minimum", vote_minimum.to_string_dec ());
	json.put ("vote_generator_delay", vote_generator_delay.count ());
	json.put ("vote_generator_threshold", vote_generator_threshold);
	json.put ("unchecked_cutoff_time", unchecked_cutoff_time.count ());
	json.put ("tcp_io_timeout", tcp_io_timeout.count ());
	json.put ("pow_sleep_interval", pow_sleep_interval.count ());
	json.put ("external_address", external_address.to_string ());
	json.put ("external_port", external_port);
	json.put ("tcp_incoming_connections_max", tcp_incoming_connections_max);
	json.put ("use_memory_pools", use_memory_pools);
	nano::jsonconfig websocket_l;
	websocket_config.serialize_json (websocket_l);
	json.put_child ("websocket", websocket_l);
	nano::jsonconfig ipc_l;
	ipc_config.serialize_json (ipc_l);
	json.put_child ("ipc", ipc_l);
	nano::jsonconfig diagnostics_l;
	diagnostics_config.serialize_json (diagnostics_l);
	json.put_child ("diagnostics", diagnostics_l);
	json.put ("confirmation_history_size", confirmation_history_size);
	json.put ("active_elections_size", active_elections_size);
	json.put ("bandwidth_limit", bandwidth_limit);

	return json.get_error ();
}

bool nano::node_config::upgrade_json (unsigned version_a, nano::jsonconfig & json)
{
	json.put ("version", json_version ());
	switch (version_a)
	{
		case 1:
		{
			auto reps_l (json.get_required_child ("preconfigured_representatives"));
			nano::jsonconfig reps;
			reps_l.array_entries<std::string> ([&reps](std::string entry) {
				nano::uint256_union account;
				account.decode_account (entry);
				reps.push (account.to_account ());
			});

			json.replace_child ("preconfigured_representatives", reps);
		}
		case 2:
		{
			json.put ("inactive_supply", nano::uint128_union (0).to_string_dec ());
			json.put ("password_fanout", std::to_string (1024));
			json.put ("io_threads", std::to_string (io_threads));
			json.put ("work_threads", std::to_string (work_threads));
		}
		case 3:
			json.erase ("receive_minimum");
			json.put ("receive_minimum", nano::xrb_ratio.convert_to<std::string> ());
		case 4:
			json.erase ("receive_minimum");
			json.put ("receive_minimum", nano::xrb_ratio.convert_to<std::string> ());
		case 5:
			json.put ("enable_voting", enable_voting);
			json.erase ("packet_delay_microseconds");
			json.erase ("rebroadcast_delay");
			json.erase ("creation_rebroadcast");
		case 6:
			json.put ("bootstrap_connections", 16);
			json.put ("callback_address", "");
			json.put ("callback_port", 0);
			json.put ("callback_target", "");
		case 7:
			json.put ("lmdb_max_dbs", 128);
		case 8:
			json.put ("bootstrap_connections_max", "64");
		case 9:
			json.put ("state_block_parse_canary", nano::block_hash (0).to_string ());
			json.put ("state_block_generate_canary", nano::block_hash (0).to_string ());
		case 10:
			json.put ("online_weight_minimum", online_weight_minimum.to_string_dec ());
			json.put ("online_weight_quorom", std::to_string (online_weight_quorum));
			json.erase ("inactive_supply");
		case 11:
		{
			// Rename
			std::string online_weight_quorum_l;
			json.get<std::string> ("online_weight_quorom", online_weight_quorum_l);
			json.erase ("online_weight_quorom");
			json.put ("online_weight_quorum", online_weight_quorum_l);
		}
		case 12:
			json.erase ("state_block_parse_canary");
			json.erase ("state_block_generate_canary");
		case 13:
			json.put ("generate_hash_votes_at", 0);
		case 14:
			json.put ("network_threads", std::to_string (network_threads));
			json.erase ("generate_hash_votes_at");
			json.put ("block_processor_batch_max_time", block_processor_batch_max_time.count ());
		case 15:
		{
			json.put ("allow_local_peers", allow_local_peers);

			// Update to the new preconfigured_peers url for rebrand if it is found (rai -> nano)
			auto peers_l (json.get_required_child (preconfigured_peers_key));
			nano::jsonconfig peers;
			peers_l.array_entries<std::string> ([&peers](std::string entry) {
				if (entry == "rai-beta.raiblocks.net")
				{
					entry = default_beta_peer_network;
				}
				else if (entry == "rai.raiblocks.net")
				{
					entry = default_live_peer_network;
				}

				peers.push (std::move (entry));
			});

			json.replace_child (preconfigured_peers_key, peers);
			json.put ("vote_minimum", vote_minimum.to_string_dec ());

			nano::jsonconfig ipc_l;
			ipc_config.serialize_json (ipc_l);
			json.put_child ("ipc", ipc_l);

			json.put (signature_checker_threads_key, signature_checker_threads);
			json.put ("unchecked_cutoff_time", unchecked_cutoff_time.count ());
		}
		case 16:
		{
			nano::jsonconfig websocket_l;
			websocket_config.serialize_json (websocket_l);
			json.put_child ("websocket", websocket_l);
			nano::jsonconfig diagnostics_l;
			diagnostics_config.serialize_json (diagnostics_l);
			json.put_child ("diagnostics", diagnostics_l);
			json.put ("tcp_io_timeout", tcp_io_timeout.count ());
			json.put (pow_sleep_interval_key, pow_sleep_interval.count ());
			json.put ("external_address", external_address.to_string ());
			json.put ("external_port", external_port);
			json.put ("tcp_incoming_connections_max", tcp_incoming_connections_max);
			json.put ("vote_generator_delay", vote_generator_delay.count ());
			json.put ("vote_generator_threshold", vote_generator_threshold);
			json.put ("use_memory_pools", use_memory_pools);
			json.put ("confirmation_history_size", confirmation_history_size);
			json.put ("active_elections_size", active_elections_size);
			json.put ("bandwidth_limit", bandwidth_limit);
			json.put ("conf_height_processor_batch_min_time", conf_height_processor_batch_min_time.count ());
		}
		case 17:
			break;
		default:
			throw std::runtime_error ("Unknown node_config version");
	}
	return version_a < json_version ();
}

nano::error nano::node_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json)
{
	try
	{
		auto version_l (json.get_optional<unsigned> ("version"));
		if (!version_l)
		{
			version_l = 1;
			json.put ("version", version_l);
			auto work_peers_l (json.get_optional_child ("work_peers"));
			if (!work_peers_l)
			{
				nano::jsonconfig empty;
				json.put_child ("work_peers", empty);
			}
			upgraded_a = true;
		}

		upgraded_a |= upgrade_json (version_l.get (), json);

		auto logging_l (json.get_required_child ("logging"));
		logging.deserialize_json (upgraded_a, logging_l);

		work_peers.clear ();
		auto work_peers_l (json.get_required_child ("work_peers"));
		work_peers_l.array_entries<std::string> ([this](std::string entry) {
			auto port_position (entry.rfind (':'));
			bool result = port_position == -1;
			if (!result)
			{
				auto port_str (entry.substr (port_position + 1));
				uint16_t port;
				result |= parse_port (port_str, port);
				if (!result)
				{
					auto address (entry.substr (0, port_position));
					this->work_peers.push_back (std::make_pair (address, port));
				}
			}
		});

		auto preconfigured_peers_l (json.get_required_child (preconfigured_peers_key));
		preconfigured_peers.clear ();
		preconfigured_peers_l.array_entries<std::string> ([this](std::string entry) {
			preconfigured_peers.push_back (entry);
		});

		auto preconfigured_representatives_l (json.get_required_child ("preconfigured_representatives"));
		preconfigured_representatives.clear ();
		preconfigured_representatives_l.array_entries<std::string> ([this, &json](std::string entry) {
			nano::account representative (0);
			if (representative.decode_account (entry))
			{
				json.get_error ().set ("Invalid representative account: " + entry);
			}
			preconfigured_representatives.push_back (representative);
		});

		if (preconfigured_representatives.empty ())
		{
			json.get_error ().set ("At least one representative account must be set");
		}
		auto stat_config_l (json.get_optional_child ("statistics"));
		if (stat_config_l)
		{
			stat_config.deserialize_json (stat_config_l.get ());
		}

		auto receive_minimum_l (json.get<std::string> ("receive_minimum"));
		if (receive_minimum.decode_dec (receive_minimum_l))
		{
			json.get_error ().set ("receive_minimum contains an invalid decimal amount");
		}

		auto online_weight_minimum_l (json.get<std::string> ("online_weight_minimum"));
		if (online_weight_minimum.decode_dec (online_weight_minimum_l))
		{
			json.get_error ().set ("online_weight_minimum contains an invalid decimal amount");
		}

		auto vote_minimum_l (json.get<std::string> ("vote_minimum"));
		if (vote_minimum.decode_dec (vote_minimum_l))
		{
			json.get_error ().set ("vote_minimum contains an invalid decimal amount");
		}

		unsigned long delay_l = vote_generator_delay.count ();
		json.get<unsigned long> ("vote_generator_delay", delay_l);
		vote_generator_delay = std::chrono::milliseconds (delay_l);

		json.get<unsigned> ("vote_generator_threshold", vote_generator_threshold);

		auto block_processor_batch_max_time_l (json.get<unsigned long> ("block_processor_batch_max_time"));
		block_processor_batch_max_time = std::chrono::milliseconds (block_processor_batch_max_time_l);
		auto unchecked_cutoff_time_l = static_cast<unsigned long> (unchecked_cutoff_time.count ());
		json.get ("unchecked_cutoff_time", unchecked_cutoff_time_l);
		unchecked_cutoff_time = std::chrono::seconds (unchecked_cutoff_time_l);

		auto tcp_io_timeout_l = static_cast<unsigned long> (tcp_io_timeout.count ());
		json.get ("tcp_io_timeout", tcp_io_timeout_l);
		tcp_io_timeout = std::chrono::seconds (tcp_io_timeout_l);

		auto ipc_config_l (json.get_optional_child ("ipc"));
		if (ipc_config_l)
		{
			ipc_config.deserialize_json (upgraded_a, ipc_config_l.get ());
		}
		auto websocket_config_l (json.get_optional_child ("websocket"));
		if (websocket_config_l)
		{
			websocket_config.deserialize_json (websocket_config_l.get ());
		}
		auto diagnostics_config_l (json.get_optional_child ("diagnostics"));
		if (diagnostics_config_l)
		{
			diagnostics_config.deserialize_json (diagnostics_config_l.get ());
		}
		json.get<uint16_t> ("peering_port", peering_port);
		json.get<unsigned> ("bootstrap_fraction_numerator", bootstrap_fraction_numerator);
		json.get<unsigned> ("online_weight_quorum", online_weight_quorum);
		json.get<unsigned> ("password_fanout", password_fanout);
		json.get<unsigned> ("io_threads", io_threads);
		json.get<unsigned> ("work_threads", work_threads);
		json.get<unsigned> ("network_threads", network_threads);
		json.get<unsigned> ("bootstrap_connections", bootstrap_connections);
		json.get<unsigned> ("bootstrap_connections_max", bootstrap_connections_max);
		json.get<std::string> ("callback_address", callback_address);
		json.get<uint16_t> ("callback_port", callback_port);
		json.get<std::string> ("callback_target", callback_target);
		json.get<int> ("lmdb_max_dbs", lmdb_max_dbs);
		json.get<bool> ("enable_voting", enable_voting);
		json.get<bool> ("allow_local_peers", allow_local_peers);
		json.get<unsigned> (signature_checker_threads_key, signature_checker_threads);
		json.get<boost::asio::ip::address_v6> ("external_address", external_address);
		json.get<uint16_t> ("external_port", external_port);
		json.get<unsigned> ("tcp_incoming_connections_max", tcp_incoming_connections_max);

		auto pow_sleep_interval_l (pow_sleep_interval.count ());
		json.get (pow_sleep_interval_key, pow_sleep_interval_l);
		pow_sleep_interval = std::chrono::nanoseconds (pow_sleep_interval_l);
		json.get<bool> ("use_memory_pools", use_memory_pools);
		json.get<size_t> ("confirmation_history_size", confirmation_history_size);
		json.get<size_t> ("active_elections_size", active_elections_size);
		json.get<size_t> ("bandwidth_limit", bandwidth_limit);

		auto conf_height_processor_batch_min_time_l (conf_height_processor_batch_min_time.count ());
		json.get ("conf_height_processor_batch_min_time", conf_height_processor_batch_min_time_l);
		conf_height_processor_batch_min_time = std::chrono::milliseconds (conf_height_processor_batch_min_time_l);

		nano::network_constants network;
		// Validate ranges
		if (online_weight_quorum > 100)
		{
			json.get_error ().set ("online_weight_quorum must be less than 100");
		}
		if (password_fanout < 16 || password_fanout > 1024 * 1024)
		{
			json.get_error ().set ("password_fanout must be a number between 16 and 1048576");
		}
		if (io_threads == 0)
		{
			json.get_error ().set ("io_threads must be non-zero");
		}
		if (active_elections_size <= 250 && !network.is_test_network ())
		{
			json.get_error ().set ("active_elections_size must be grater than 250");
		}
		if (bandwidth_limit > std::numeric_limits<size_t>::max ())
		{
			json.get_error ().set ("bandwidth_limit unbounded = 0, default = 5242880, max = 18446744073709551615");
		}
		if (vote_generator_threshold < 1 || vote_generator_threshold > 12)
		{
			json.get_error ().set ("vote_generator_threshold must be a number between 1 and 12");
		}
	}
	catch (std::runtime_error const & ex)
	{
		json.get_error ().set (ex.what ());
	}
	return json.get_error ();
}

nano::account nano::node_config::random_representative ()
{
	assert (!preconfigured_representatives.empty ());
	size_t index (nano::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (preconfigured_representatives.size () - 1)));
	auto result (preconfigured_representatives[index]);
	return result;
}
