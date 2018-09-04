#include <rai/lib/errors.hpp>
#include <rai/lib/interface.h>
#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

std::shared_ptr<rai::wallet> rai::rpc_handler::wallet_impl ()
{
	if (!ec)
	{
		std::string wallet_text (request.get<std::string> ("wallet"));
		rai::uint256_union wallet;
		if (!wallet.decode_hex (wallet_text))
		{
			auto existing (node.wallets.items.find (wallet));
			if (existing != node.wallets.items.end ())
			{
				return existing->second;
			}
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		else
		{
			ec = nano::error_common::bad_wallet_number;
		}
	}
	return nullptr;
}

void rai::rpc_handler::search_pending ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto error (wallet->search_pending ());
		response_l.put ("started", !error);
	}
	response_errors ();
}

void rai::rpc_handler::search_pending_all ()
{
	rpc_control_impl ();
	if (!ec)
	{
		node.wallets.search_pending_all ();
		response_l.put ("success", "");
	}
	response_errors ();
}

void rai::rpc_handler::receive ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	auto hash (hash_impl ("block"));
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		if (wallet->store.valid_password (transaction))
		{
			if (wallet->store.find (transaction, account) != wallet->store.end ())
			{
				auto block (node.store.block_get (transaction, hash));
				if (block != nullptr)
				{
					if (node.store.pending_exists (transaction, rai::pending_key (account, hash)))
					{
						auto work (work_optional_impl ());
						if (!ec && work)
						{
							rai::account_info info;
							rai::uint256_union head;
							if (!node.store.account_get (transaction, account, info))
							{
								head = info.head;
							}
							else
							{
								head = account;
							}
							if (!rai::work_validate (head, work))
							{
								auto transaction_a (node.store.tx_begin_write ());
								wallet->store.work_put (transaction_a, account, work);
							}
							else
							{
								ec = nano::error_common::invalid_work;
							}
						}
						if (!ec)
						{
							auto response_a (response);
							wallet->receive_async (std::move (block), account, rai::genesis_amount, [response_a](std::shared_ptr<rai::block> block_a) {
								rai::uint256_union hash_a (0);
								if (block_a != nullptr)
								{
									hash_a = block_a->hash ();
								}
								boost::property_tree::ptree response_l;
								response_l.put ("block", hash_a.to_string ());
								response_a (response_l);
							},
							work == 0);
						}
					}
					else
					{
						ec = nano::error_process::unreceivable;
					}
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_common::account_not_found_wallet;
			}
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	// Because of receive_async
	if (ec)
	{
		response_errors ();
	}
}

void rai::rpc_handler::send ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto amount (amount_impl ());
	if (!ec)
	{
		std::string source_text (request.get<std::string> ("source"));
		rai::account source;
		if (!source.decode_account (source_text))
		{
			std::string destination_text (request.get<std::string> ("destination"));
			rai::account destination;
			if (!destination.decode_account (destination_text))
			{
				auto work (work_optional_impl ());
				rai::uint128_t balance (0);
				if (!ec)
				{
					auto transaction (node.store.tx_begin (work != 0)); // false if no "work" in request, true if work > 0
					if (wallet->store.valid_password (transaction))
					{
						rai::account_info info;
						if (!node.store.account_get (transaction, source, info))
						{
							balance = (info.balance).number ();
						}
						else
						{
							ec = nano::error_common::account_not_found;
						}
						if (!ec && work)
						{
							if (!rai::work_validate (info.head, work))
							{
								wallet->store.work_put (transaction, source, work);
							}
							else
							{
								ec = nano::error_common::invalid_work;
							}
						}
					}
					else
					{
						ec = nano::error_common::wallet_locked;
					}
				}
				if (!ec)
				{
					boost::optional<std::string> send_id (request.get_optional<std::string> ("id"));
					auto rpc_l (shared_from_this ());
					auto response_a (response);
					wallet->send_async (source, destination, amount.number (), [balance, amount, response_a](std::shared_ptr<rai::block> block_a) {
						if (block_a != nullptr)
						{
							rai::uint256_union hash (block_a->hash ());
							boost::property_tree::ptree response_l;
							response_l.put ("block", hash.to_string ());
							response_a (response_l);
						}
						else
						{
							if (balance >= amount.number ())
							{
								error_response (response_a, "Error generating block");
							}
							else
							{
								std::error_code ec (nano::error_common::insufficient_balance);
								error_response (response_a, ec.message ());
							}
						}
					},
					work == 0, send_id);
				}
			}
			else
			{
				ec = nano::error_rpc::bad_destination;
			}
		}
		else
		{
			ec = nano::error_rpc::bad_source;
		}
	}
	// Because of send_async
	if (ec)
	{
		response_errors ();
	}
}

void rai::rpc_handler::wallet_add ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string key_text (request.get<std::string> ("key"));
		rai::raw_key key;
		if (!key.data.decode_hex (key_text))
		{
			const bool generate_work = request.get<bool> ("work", true);
			auto pub (wallet->insert_adhoc (key, generate_work));
			if (!pub.is_zero ())
			{
				response_l.put ("account", pub.to_account ());
			}
			else
			{
				ec = nano::error_common::wallet_locked;
			}
		}
		else
		{
			ec = nano::error_common::bad_private_key;
		}
	}
	response_errors ();
}

void rai::rpc_handler::wallet_add_watch ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		if (wallet->store.valid_password (transaction))
		{
			for (auto & accounts : request.get_child ("accounts"))
			{
				auto account (account_impl (accounts.second.data ()));
				if (!ec)
				{
					wallet->insert_watch (transaction, account);
				}
			}
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	response_errors ();
}

void rai::rpc_handler::wallet_info ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		rai::uint128_t balance (0);
		rai::uint128_t pending (0);
		uint64_t count (0);
		uint64_t deterministic_count (0);
		uint64_t adhoc_count (0);
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			balance = balance + node.ledger.account_balance (transaction, account);
			pending = pending + node.ledger.account_pending (transaction, account);
			rai::key_type key_type (wallet->store.key_type (i->second));
			if (key_type == rai::key_type::deterministic)
			{
				deterministic_count++;
			}
			else if (key_type == rai::key_type::adhoc)
			{
				adhoc_count++;
			}
			count++;
		}
		uint32_t deterministic_index (wallet->store.deterministic_index_get (transaction));
		response_l.put ("balance", balance.convert_to<std::string> ());
		response_l.put ("pending", pending.convert_to<std::string> ());
		response_l.put ("accounts_count", std::to_string (count));
		response_l.put ("deterministic_count", std::to_string (deterministic_count));
		response_l.put ("adhoc_count", std::to_string (adhoc_count));
		response_l.put ("deterministic_index", std::to_string (deterministic_index));
	}
	response_errors ();
}

void rai::rpc_handler::wallet_balances ()
{
	auto wallet (wallet_impl ());
	auto threshold (threshold_optional_impl ());
	if (!ec)
	{
		boost::property_tree::ptree balances;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			rai::uint128_t balance = node.ledger.account_balance (transaction, account);
			if (balance >= threshold.number ())
			{
				boost::property_tree::ptree entry;
				rai::uint128_t pending = node.ledger.account_pending (transaction, account);
				entry.put ("balance", balance.convert_to<std::string> ());
				entry.put ("pending", pending.convert_to<std::string> ());
				balances.push_back (std::make_pair (account.to_account (), entry));
			}
		}
		response_l.add_child ("balances", balances);
	}
	response_errors ();
}

void rai::rpc_handler::wallet_change_seed ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string seed_text (request.get<std::string> ("seed"));
		rai::raw_key seed;
		if (!seed.data.decode_hex (seed_text))
		{
			auto transaction (node.store.tx_begin_write ());
			if (wallet->store.valid_password (transaction))
			{
				wallet->change_seed (transaction, seed);
				response_l.put ("success", "");
			}
			else
			{
				ec = nano::error_common::wallet_locked;
			}
		}
		else
		{
			ec = nano::error_common::bad_seed;
		}
	}
	response_errors ();
}

void rai::rpc_handler::wallet_contains ()
{
	auto account (account_impl ());
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto exists (wallet->store.find (transaction, account) != wallet->store.end ());
		response_l.put ("exists", exists ? "1" : "0");
	}
	response_errors ();
}

void rai::rpc_handler::wallet_create ()
{
	rpc_control_impl ();
	if (!ec)
	{
		rai::keypair wallet_id;
		node.wallets.create (wallet_id.pub);
		auto transaction (node.store.tx_begin_read ());
		auto existing (node.wallets.items.find (wallet_id.pub));
		if (existing != node.wallets.items.end ())
		{
			response_l.put ("wallet", wallet_id.pub.to_string ());
		}
		else
		{
			ec = nano::error_common::wallet_lmdb_max_dbs;
		}
	}
	response_errors ();
}

void rai::rpc_handler::wallet_destroy ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string wallet_text (request.get<std::string> ("wallet"));
		rai::uint256_union wallet;
		if (!wallet.decode_hex (wallet_text))
		{
			auto existing (node.wallets.items.find (wallet));
			if (existing != node.wallets.items.end ())
			{
				node.wallets.destroy (wallet);
				bool destroyed (node.wallets.items.find (wallet) == node.wallets.items.end ());
				response_l.put ("destroyed", destroyed ? "1" : "0");
			}
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		else
		{
			ec = nano::error_common::bad_wallet_number;
		}
	}
	response_errors ();
}

void rai::rpc_handler::wallet_export ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		std::string json;
		wallet->store.serialize_json (transaction, json);
		response_l.put ("json", json);
	}
	response_errors ();
}

void rai::rpc_handler::wallet_frontiers ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree frontiers;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			auto latest (node.ledger.latest (transaction, account));
			if (!latest.is_zero ())
			{
				frontiers.put (account.to_account (), latest.to_string ());
			}
		}
		response_l.add_child ("frontiers", frontiers);
	}
	response_errors ();
}

void rai::rpc_handler::wallet_key_valid ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto valid (wallet->store.valid_password (transaction));
		response_l.put ("valid", valid ? "1" : "0");
	}
	response_errors ();
}

void rai::rpc_handler::wallet_ledger ()
{
	const bool representative = request.get<bool> ("representative", false);
	const bool weight = request.get<bool> ("weight", false);
	const bool pending = request.get<bool> ("pending", false);
	uint64_t modified_since (0);
	boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
	if (modified_since_text.is_initialized ())
	{
		modified_since = strtoul (modified_since_text.get ().c_str (), NULL, 10);
	}
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree accounts;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			rai::account_info info;
			if (!node.store.account_get (transaction, account, info))
			{
				if (info.modified >= modified_since)
				{
					boost::property_tree::ptree entry;
					entry.put ("frontier", info.head.to_string ());
					entry.put ("open_block", info.open_block.to_string ());
					entry.put ("representative_block", info.rep_block.to_string ());
					std::string balance;
					rai::uint128_union (info.balance).encode_dec (balance);
					entry.put ("balance", balance);
					entry.put ("modified_timestamp", std::to_string (info.modified));
					entry.put ("block_count", std::to_string (info.block_count));
					if (representative)
					{
						auto block (node.store.block_get (transaction, info.rep_block));
						assert (block != nullptr);
						entry.put ("representative", block->representative ().to_account ());
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight (transaction, account));
						entry.put ("weight", account_weight.convert_to<std::string> ());
					}
					if (pending)
					{
						auto account_pending (node.ledger.account_pending (transaction, account));
						entry.put ("pending", account_pending.convert_to<std::string> ());
					}
					accounts.push_back (std::make_pair (account.to_account (), entry));
				}
			}
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void rai::rpc_handler::wallet_lock ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		rai::raw_key empty;
		empty.data.clear ();
		wallet->store.password.value_set (empty);
		response_l.put ("locked", "1");
	}
	response_errors ();
}

void rai::rpc_handler::wallet_pending ()
{
	auto wallet (wallet_impl ());
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	const bool source = request.get<bool> ("source", false);
	const bool min_version = request.get<bool> ("min_version", false);
	const bool include_active = request.get<bool> ("include_active", false);
	if (!ec)
	{
		boost::property_tree::ptree pending;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			boost::property_tree::ptree peers_l;
			rai::account end (account.number () + 1);
			for (auto ii (node.store.pending_begin (transaction, rai::pending_key (account, 0))), nn (node.store.pending_begin (transaction, rai::pending_key (end, 0))); ii != nn && peers_l.size () < count; ++ii)
			{
				rai::pending_key key (ii->first);
				std::shared_ptr<rai::block> block (node.store.block_get (transaction, key.hash));
				assert (block);
				if (include_active || (block && !node.active.active (*block)))
				{
					if (threshold.is_zero () && !source)
					{
						boost::property_tree::ptree entry;
						entry.put ("", key.hash.to_string ());
						peers_l.push_back (std::make_pair ("", entry));
					}
					else
					{
						rai::pending_info info (ii->second);
						if (info.amount.number () >= threshold.number ())
						{
							if (source || min_version)
							{
								boost::property_tree::ptree pending_tree;
								pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
								if (source)
								{
									pending_tree.put ("source", info.source.to_account ());
								}
								if (min_version)
								{
									pending_tree.put ("min_version", info.epoch == rai::epoch::epoch_1 ? "1" : "0");
								}
								peers_l.add_child (key.hash.to_string (), pending_tree);
							}
							else
							{
								peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
							}
						}
					}
				}
			}
			if (!peers_l.empty ())
			{
				pending.add_child (account.to_account (), peers_l);
			}
		}
		response_l.add_child ("blocks", pending);
	}
	response_errors ();
}

void rai::rpc_handler::wallet_representative ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		response_l.put ("representative", wallet->store.representative (transaction).to_account ());
	}
	response_errors ();
}

void rai::rpc_handler::wallet_representative_set ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string representative_text (request.get<std::string> ("representative"));
		rai::account representative;
		if (!representative.decode_account (representative_text))
		{
			auto transaction (node.store.tx_begin_write ());
			wallet->store.representative_set (transaction, representative);
			response_l.put ("set", "1");
		}
		else
		{
			ec = nano::error_rpc::bad_representative_number;
		}
	}
	response_errors ();
}

void rai::rpc_handler::wallet_republish ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		boost::property_tree::ptree blocks;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			auto latest (node.ledger.latest (transaction, account));
			std::unique_ptr<rai::block> block;
			std::vector<rai::block_hash> hashes;
			while (!latest.is_zero () && hashes.size () < count)
			{
				hashes.push_back (latest);
				block = node.store.block_get (transaction, latest);
				latest = block->previous ();
			}
			std::reverse (hashes.begin (), hashes.end ());
			for (auto & hash : hashes)
			{
				block = node.store.block_get (transaction, hash);
				node.network.republish_block (transaction, std::move (block));
				boost::property_tree::ptree entry;
				entry.put ("", hash.to_string ());
				blocks.push_back (std::make_pair ("", entry));
			}
		}
		response_l.add_child ("blocks", blocks);
	}
	response_errors ();
}

void rai::rpc_handler::wallet_work_get ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree works;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			rai::account account (i->first);
			uint64_t work (0);
			auto error_work (wallet->store.work_get (transaction, account, work));
			works.put (account.to_account (), rai::to_string_hex (work));
		}
		response_l.add_child ("works", works);
	}
	response_errors ();
}
