#include <rai/lib/errors.hpp>
#include <rai/lib/interface.h>
#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

rai::rpc_handler::rpc_handler (rai::node & node_a, rai::rpc & rpc_a, std::string const & body_a, std::string const & request_id_a, std::function<void(boost::property_tree::ptree const &)> const & response_a) :
body (body_a),
node (node_a),
rpc (rpc_a),
response (response_a),
request_id (request_id_a)
{
}

void rai::error_response (std::function<void(boost::property_tree::ptree const &)> response_a, std::string const & message_a)
{
	boost::property_tree::ptree response_l;
	response_l.put ("error", message_a);
	response_a (response_l);
}

void rai::rpc_handler::response_errors ()
{
	if (ec || response_l.empty ())
	{
		boost::property_tree::ptree response_error;
		response_error.put ("error", ec ? ec.message () : "Empty response");
		response (response_error);
	}
	else
	{
		response (response_l);
	}
}

rai::amount rai::rpc_handler::amount_impl ()
{
	rai::amount result (0);
	if (!ec)
	{
		std::string amount_text (request.get<std::string> ("amount"));
		if (result.decode_dec (amount_text))
		{
			ec = nano::error_common::invalid_amount;
		}
	}
	return result;
}

rai::block_hash rai::rpc_handler::hash_impl (std::string search_text)
{
	rai::block_hash result (0);
	if (!ec)
	{
		std::string hash_text (request.get<std::string> (search_text));
		if (result.decode_hex (hash_text))
		{
			ec = nano::error_blocks::invalid_block_hash;
		}
	}
	return result;
}

rai::amount rai::rpc_handler::threshold_optional_impl ()
{
	rai::amount result (0);
	boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
	if (!ec && threshold_text.is_initialized ())
	{
		if (result.decode_dec (threshold_text.get ()))
		{
			ec = nano::error_common::bad_threshold;
		}
	}
	return result;
}

uint64_t rai::rpc_handler::count_impl ()
{
	uint64_t result (0);
	if (!ec)
	{
		std::string count_text (request.get<std::string> ("count"));
		if (rai::decode_unsigned (count_text, result) || result == 0)
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t rai::rpc_handler::count_optional_impl (uint64_t result)
{
	boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
	if (!ec && count_text.is_initialized ())
	{
		if (rai::decode_unsigned (count_text.get (), result))
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

bool rai::rpc_handler::rpc_control_impl ()
{
	bool result (false);
	if (!ec)
	{
		if (!rpc.config.enable_control)
		{
			ec = nano::error_rpc::rpc_control_disabled;
		}
		else
		{
			result = true;
		}
	}
	return result;
}

void rai::rpc_handler::available_supply ()
{
	auto genesis_balance (node.balance (rai::genesis_account)); // Cold storage genesis
	auto landing_balance (node.balance (rai::account ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"))); // Active unavailable account
	auto faucet_balance (node.balance (rai::account ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"))); // Faucet account
	auto burned_balance ((node.balance_pending (rai::account (0))).second); // Burning 0 account
	auto available (rai::genesis_amount - genesis_balance - landing_balance - faucet_balance - burned_balance);
	response_l.put ("available", available.convert_to<std::string> ());
	response_errors ();
}

void rai::rpc_handler::bootstrap ()
{
	std::string address_text = request.get<std::string> ("address");
	std::string port_text = request.get<std::string> ("port");
	boost::system::error_code address_ec;
	auto address (boost::asio::ip::address_v6::from_string (address_text, address_ec));
	if (!address_ec)
	{
		uint16_t port;
		if (!rai::parse_port (port_text, port))
		{
			node.bootstrap_initiator.bootstrap (rai::endpoint (address, port));
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	else
	{
		ec = nano::error_common::invalid_ip_address;
	}
	response_errors ();
}

void rai::rpc_handler::bootstrap_any ()
{
	node.bootstrap_initiator.bootstrap ();
	response_l.put ("success", "");
	response_errors ();
}

void rai::rpc_handler::deterministic_key ()
{
	std::string seed_text (request.get<std::string> ("seed"));
	std::string index_text (request.get<std::string> ("index"));
	rai::raw_key seed;
	if (!seed.data.decode_hex (seed_text))
	{
		try
		{
			uint32_t index (std::stoul (index_text));
			rai::uint256_union prv;
			rai::deterministic_key (seed.data, index, prv);
			rai::uint256_union pub (rai::pub_key (prv));
			response_l.put ("private", prv.to_string ());
			response_l.put ("public", pub.to_string ());
			response_l.put ("account", pub.to_account ());
		}
		catch (std::logic_error const &)
		{
			ec = nano::error_common::invalid_index;
		}
	}
	else
	{
		ec = nano::error_common::bad_seed;
	}
	response_errors ();
}

void rai::rpc_handler::keepalive ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string address_text (request.get<std::string> ("address"));
		std::string port_text (request.get<std::string> ("port"));
		uint16_t port;
		if (!rai::parse_port (port_text, port))
		{
			node.keepalive (address_text, port);
			response_l.put ("started", "1");
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	response_errors ();
}

void rai::rpc_handler::key_create ()
{
	rai::keypair pair;
	response_l.put ("private", pair.prv.data.to_string ());
	response_l.put ("public", pair.pub.to_string ());
	response_l.put ("account", pair.pub.to_account ());
	response_errors ();
}

void rai::rpc_handler::key_expand ()
{
	std::string key_text (request.get<std::string> ("key"));
	rai::uint256_union prv;
	if (!prv.decode_hex (key_text))
	{
		rai::uint256_union pub (rai::pub_key (prv));
		response_l.put ("private", prv.to_string ());
		response_l.put ("public", pub.to_string ());
		response_l.put ("account", pub.to_account ());
	}
	else
	{
		ec = nano::error_common::bad_private_key;
	}
	response_errors ();
}

void rai::rpc_handler::ledger ()
{
	rpc_control_impl ();
	auto count (count_optional_impl ());
	if (!ec)
	{
		rai::account start (0);
		boost::optional<std::string> account_text (request.get_optional<std::string> ("account"));
		if (account_text.is_initialized ())
		{
			if (start.decode_account (account_text.get ()))
			{
				ec = nano::error_common::bad_account_number;
			}
		}
		uint64_t modified_since (0);
		boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
		if (modified_since_text.is_initialized ())
		{
			modified_since = strtoul (modified_since_text.get ().c_str (), NULL, 10);
		}
		const bool sorting = request.get<bool> ("sorting", false);
		const bool representative = request.get<bool> ("representative", false);
		const bool weight = request.get<bool> ("weight", false);
		const bool pending = request.get<bool> ("pending", false);
		boost::property_tree::ptree accounts;
		auto transaction (node.store.tx_begin_read ());
		if (!ec && !sorting) // Simple
		{
			for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && accounts.size () < count; ++i)
			{
				rai::account_info info (i->second);
				if (info.modified >= modified_since)
				{
					rai::account account (i->first);
					boost::property_tree::ptree response_a;
					response_a.put ("frontier", info.head.to_string ());
					response_a.put ("open_block", info.open_block.to_string ());
					response_a.put ("representative_block", info.rep_block.to_string ());
					std::string balance;
					rai::uint128_union (info.balance).encode_dec (balance);
					response_a.put ("balance", balance);
					response_a.put ("modified_timestamp", std::to_string (info.modified));
					response_a.put ("block_count", std::to_string (info.block_count));
					if (representative)
					{
						auto block (node.store.block_get (transaction, info.rep_block));
						assert (block != nullptr);
						response_a.put ("representative", block->representative ().to_account ());
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight (transaction, account));
						response_a.put ("weight", account_weight.convert_to<std::string> ());
					}
					if (pending)
					{
						auto account_pending (node.ledger.account_pending (transaction, account));
						response_a.put ("pending", account_pending.convert_to<std::string> ());
					}
					accounts.push_back (std::make_pair (account.to_account (), response_a));
				}
			}
		}
		else if (!ec) // Sorting
		{
			std::vector<std::pair<rai::uint128_union, rai::account>> ledger_l;
			for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n; ++i)
			{
				rai::account_info info (i->second);
				rai::uint128_union balance (info.balance);
				if (info.modified >= modified_since)
				{
					ledger_l.push_back (std::make_pair (balance, rai::account (i->first)));
				}
			}
			std::sort (ledger_l.begin (), ledger_l.end ());
			std::reverse (ledger_l.begin (), ledger_l.end ());
			rai::account_info info;
			for (auto i (ledger_l.begin ()), n (ledger_l.end ()); i != n && accounts.size () < count; ++i)
			{
				node.store.account_get (transaction, i->second, info);
				rai::account account (i->second);
				boost::property_tree::ptree response_a;
				response_a.put ("frontier", info.head.to_string ());
				response_a.put ("open_block", info.open_block.to_string ());
				response_a.put ("representative_block", info.rep_block.to_string ());
				std::string balance;
				(i->first).encode_dec (balance);
				response_a.put ("balance", balance);
				response_a.put ("modified_timestamp", std::to_string (info.modified));
				response_a.put ("block_count", std::to_string (info.block_count));
				if (representative)
				{
					auto block (node.store.block_get (transaction, info.rep_block));
					assert (block != nullptr);
					response_a.put ("representative", block->representative ().to_account ());
				}
				if (weight)
				{
					auto account_weight (node.ledger.weight (transaction, account));
					response_a.put ("weight", account_weight.convert_to<std::string> ());
				}
				if (pending)
				{
					auto account_pending (node.ledger.account_pending (transaction, account));
					response_a.put ("pending", account_pending.convert_to<std::string> ());
				}
				accounts.push_back (std::make_pair (account.to_account (), response_a));
			}
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void rai::rpc_handler::mrai_from_raw (rai::uint128_t ratio)
{
	auto amount (amount_impl ());
	if (!ec)
	{
		auto result (amount.number () / ratio);
		response_l.put ("amount", result.convert_to<std::string> ());
	}
	response_errors ();
}

void rai::rpc_handler::mrai_to_raw (rai::uint128_t ratio)
{
	auto amount (amount_impl ());
	if (!ec)
	{
		auto result (amount.number () * ratio);
		if (result > amount.number ())
		{
			response_l.put ("amount", result.convert_to<std::string> ());
		}
		else
		{
			ec = nano::error_common::invalid_amount_big;
		}
	}
	response_errors ();
}

void rai::rpc_handler::password_change ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		std::string password_text (request.get<std::string> ("password"));
		auto error (wallet->store.rekey (transaction, password_text));
		response_l.put ("changed", error ? "0" : "1");
	}
	response_errors ();
}

void rai::rpc_handler::password_enter ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string password_text (request.get<std::string> ("password"));
		auto transaction (node.store.tx_begin_write ());
		auto error (wallet->enter_password (transaction, password_text));
		response_l.put ("valid", error ? "0" : "1");
	}
	response_errors ();
}

void rai::rpc_handler::password_valid (bool wallet_locked)
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto valid (wallet->store.valid_password (transaction));
		if (!wallet_locked)
		{
			response_l.put ("valid", valid ? "1" : "0");
		}
		else
		{
			response_l.put ("locked", valid ? "0" : "1");
		}
	}
	response_errors ();
}

void rai::rpc_handler::peers ()
{
	boost::property_tree::ptree peers_l;
	auto peers_list (node.peers.list_version ());
	for (auto i (peers_list.begin ()), n (peers_list.end ()); i != n; ++i)
	{
		std::stringstream text;
		text << i->first;
		peers_l.push_back (boost::property_tree::ptree::value_type (text.str (), boost::property_tree::ptree (std::to_string (i->second))));
	}
	response_l.add_child ("peers", peers_l);
	response_errors ();
}

void rai::rpc_handler::receive_minimum ()
{
	rpc_control_impl ();
	if (!ec)
	{
		response_l.put ("amount", node.config.receive_minimum.to_string_dec ());
	}
	response_errors ();
}

void rai::rpc_handler::receive_minimum_set ()
{
	rpc_control_impl ();
	auto amount (amount_impl ());
	if (!ec)
	{
		node.config.receive_minimum = amount;
		response_l.put ("success", "");
	}
	response_errors ();
}

void rai::rpc_handler::representatives ()
{
	auto count (count_optional_impl ());
	if (!ec)
	{
		const bool sorting = request.get<bool> ("sorting", false);
		boost::property_tree::ptree representatives;
		auto transaction (node.store.tx_begin_read ());
		if (!sorting) // Simple
		{
			for (auto i (node.store.representation_begin (transaction)), n (node.store.representation_end ()); i != n && representatives.size () < count; ++i)
			{
				rai::account account (i->first);
				auto amount (node.store.representation_get (transaction, account));
				representatives.put (account.to_account (), amount.convert_to<std::string> ());
			}
		}
		else // Sorting
		{
			std::vector<std::pair<rai::uint128_union, std::string>> representation;
			for (auto i (node.store.representation_begin (transaction)), n (node.store.representation_end ()); i != n; ++i)
			{
				rai::account account (i->first);
				auto amount (node.store.representation_get (transaction, account));
				representation.push_back (std::make_pair (amount, account.to_account ()));
			}
			std::sort (representation.begin (), representation.end ());
			std::reverse (representation.begin (), representation.end ());
			for (auto i (representation.begin ()), n (representation.end ()); i != n && representatives.size () < count; ++i)
			{
				representatives.put (i->second, (i->first).number ().convert_to<std::string> ());
			}
		}
		response_l.add_child ("representatives", representatives);
	}
	response_errors ();
}

void rai::rpc_handler::representatives_online ()
{
	boost::property_tree::ptree representatives;
	auto reps (node.online_reps.list ());
	for (auto & i : reps)
	{
		representatives.put (i.to_account (), "");
	}
	response_l.add_child ("representatives", representatives);
	response_errors ();
}

void rai::rpc_handler::republish ()
{
	auto count (count_optional_impl (1024U));
	uint64_t sources (0);
	uint64_t destinations (0);
	boost::optional<std::string> sources_text (request.get_optional<std::string> ("sources"));
	if (!ec && sources_text.is_initialized ())
	{
		if (rai::decode_unsigned (sources_text.get (), sources))
		{
			ec = nano::error_rpc::invalid_sources;
		}
	}
	boost::optional<std::string> destinations_text (request.get_optional<std::string> ("destinations"));
	if (!ec && destinations_text.is_initialized ())
	{
		if (rai::decode_unsigned (destinations_text.get (), destinations))
		{
			ec = nano::error_rpc::invalid_destinations;
		}
	}
	auto hash (hash_impl ());
	if (!ec)
	{
		boost::property_tree::ptree blocks;
		auto transaction (node.store.tx_begin_read ());
		auto block (node.store.block_get (transaction, hash));
		if (block != nullptr)
		{
			for (auto i (0); !hash.is_zero () && i < count; ++i)
			{
				block = node.store.block_get (transaction, hash);
				if (sources != 0) // Republish source chain
				{
					rai::block_hash source (node.ledger.block_source (transaction, *block));
					std::unique_ptr<rai::block> block_a (node.store.block_get (transaction, source));
					std::vector<rai::block_hash> hashes;
					while (block_a != nullptr && hashes.size () < sources)
					{
						hashes.push_back (source);
						source = block_a->previous ();
						block_a = node.store.block_get (transaction, source);
					}
					std::reverse (hashes.begin (), hashes.end ());
					for (auto & hash_l : hashes)
					{
						block_a = node.store.block_get (transaction, hash_l);
						node.network.republish_block (transaction, std::move (block_a));
						boost::property_tree::ptree entry_l;
						entry_l.put ("", hash_l.to_string ());
						blocks.push_back (std::make_pair ("", entry_l));
					}
				}
				node.network.republish_block (transaction, std::move (block)); // Republish block
				boost::property_tree::ptree entry;
				entry.put ("", hash.to_string ());
				blocks.push_back (std::make_pair ("", entry));
				if (destinations != 0) // Republish destination chain
				{
					auto block_b (node.store.block_get (transaction, hash));
					auto destination (node.ledger.block_destination (transaction, *block_b));
					if (!destination.is_zero ())
					{
						if (!node.store.pending_exists (transaction, rai::pending_key (destination, hash)))
						{
							rai::block_hash previous (node.ledger.latest (transaction, destination));
							std::unique_ptr<rai::block> block_d (node.store.block_get (transaction, previous));
							rai::block_hash source;
							std::vector<rai::block_hash> hashes;
							while (block_d != nullptr && hash != source)
							{
								hashes.push_back (previous);
								source = node.ledger.block_source (transaction, *block_d);
								previous = block_d->previous ();
								block_d = node.store.block_get (transaction, previous);
							}
							std::reverse (hashes.begin (), hashes.end ());
							if (hashes.size () > destinations)
							{
								hashes.resize (destinations);
							}
							for (auto & hash_l : hashes)
							{
								block_d = node.store.block_get (transaction, hash_l);
								node.network.republish_block (transaction, std::move (block_d));
								boost::property_tree::ptree entry_l;
								entry_l.put ("", hash_l.to_string ());
								blocks.push_back (std::make_pair ("", entry_l));
							}
						}
					}
				}
				hash = node.store.block_successor (transaction, hash);
			}
			response_l.put ("success", ""); // obsolete
			response_l.add_child ("blocks", blocks);
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::stats ()
{
	auto sink = node.stats.log_sink_json ();
	std::string type (request.get<std::string> ("type", ""));
	if (type == "counters")
	{
		node.stats.log_counters (*sink);
	}
	else if (type == "samples")
	{
		node.stats.log_samples (*sink);
	}
	else
	{
		ec = nano::error_rpc::invalid_missing_type;
	}
	if (!ec)
	{
		response (*static_cast<boost::property_tree::ptree *> (sink->to_object ()));
	}
	else
	{
		response_errors ();
	}
}

void rai::rpc_handler::stop ()
{
	rpc_control_impl ();
	if (!ec)
	{
		response_l.put ("success", "");
	}
	response_errors ();
	if (!ec)
	{
		rpc.stop ();
		node.stop ();
	}
}

void rai::rpc_handler::version ()
{
	response_l.put ("rpc_version", "1");
	response_l.put ("store_version", std::to_string (node.store_version ()));
	response_l.put ("node_vendor", boost::str (boost::format ("RaiBlocks %1%.%2%") % RAIBLOCKS_VERSION_MAJOR % RAIBLOCKS_VERSION_MINOR));
	response_errors ();
}

void rai::rpc_handler::validate_account_number ()
{
	std::string account_text (request.get<std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_account (account_text));
	response_l.put ("valid", error ? "0" : "1");
	response_errors ();
}
