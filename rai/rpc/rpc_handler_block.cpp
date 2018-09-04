#include <rai/lib/errors.hpp>
#include <rai/lib/interface.h>
#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

void rai::rpc_handler::block ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto block (node.store.block_get (transaction, hash));
		if (block != nullptr)
		{
			std::string contents;
			block->serialize_json (contents);
			response_l.put ("contents", contents);
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::blocks ()
{
	std::vector<std::string> hashes;
	boost::property_tree::ptree blocks;
	auto transaction (node.store.tx_begin_read ());
	for (boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
	{
		if (!ec)
		{
			std::string hash_text = hashes.second.data ();
			rai::uint256_union hash;
			if (!hash.decode_hex (hash_text))
			{
				auto block (node.store.block_get (transaction, hash));
				if (block != nullptr)
				{
					std::string contents;
					block->serialize_json (contents);
					blocks.put (hash_text, contents);
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_blocks::bad_hash_number;
			}
		}
	}
	response_l.add_child ("blocks", blocks);
	response_errors ();
}

void rai::rpc_handler::block_confirm ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto block_l (node.store.block_get (transaction, hash));
		if (block_l != nullptr)
		{
			node.block_confirm (std::move (block_l));
			response_l.put ("started", "1");
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::blocks_info ()
{
	const bool pending = request.get<bool> ("pending", false);
	const bool source = request.get<bool> ("source", false);
	const bool balance = request.get<bool> ("balance", false);
	std::vector<std::string> hashes;
	boost::property_tree::ptree blocks;
	auto transaction (node.store.tx_begin_read ());
	for (boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
	{
		if (!ec)
		{
			std::string hash_text = hashes.second.data ();
			rai::uint256_union hash;
			if (!hash.decode_hex (hash_text))
			{
				auto block (node.store.block_get (transaction, hash));
				if (block != nullptr)
				{
					boost::property_tree::ptree entry;
					auto account (node.ledger.account (transaction, hash));
					entry.put ("block_account", account.to_account ());
					auto amount (node.ledger.amount (transaction, hash));
					entry.put ("amount", amount.convert_to<std::string> ());
					std::string contents;
					block->serialize_json (contents);
					entry.put ("contents", contents);
					if (pending)
					{
						bool exists (false);
						auto destination (node.ledger.block_destination (transaction, *block));
						if (!destination.is_zero ())
						{
							exists = node.store.pending_exists (transaction, rai::pending_key (destination, hash));
						}
						entry.put ("pending", exists ? "1" : "0");
					}
					if (source)
					{
						rai::block_hash source_hash (node.ledger.block_source (transaction, *block));
						std::unique_ptr<rai::block> block_a (node.store.block_get (transaction, source_hash));
						if (block_a != nullptr)
						{
							auto source_account (node.ledger.account (transaction, source_hash));
							entry.put ("source_account", source_account.to_account ());
						}
						else
						{
							entry.put ("source_account", "0");
						}
					}
					if (balance)
					{
						auto balance (node.ledger.balance (transaction, hash));
						entry.put ("balance", balance.convert_to<std::string> ());
					}
					blocks.push_back (std::make_pair (hash_text, entry));
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_blocks::bad_hash_number;
			}
		}
	}
	response_l.add_child ("blocks", blocks);
	response_errors ();
}

void rai::rpc_handler::block_account ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		if (node.store.block_exists (transaction, hash))
		{
			auto account (node.ledger.account (transaction, hash));
			response_l.put ("account", account.to_account ());
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::block_count ()
{
	auto transaction (node.store.tx_begin_read ());
	response_l.put ("count", std::to_string (node.store.block_count (transaction).sum ()));
	response_l.put ("unchecked", std::to_string (node.store.unchecked_count (transaction)));
	response_errors ();
}

void rai::rpc_handler::block_count_type ()
{
	auto transaction (node.store.tx_begin_read ());
	rai::block_counts count (node.store.block_count (transaction));
	response_l.put ("send", std::to_string (count.send));
	response_l.put ("receive", std::to_string (count.receive));
	response_l.put ("open", std::to_string (count.open));
	response_l.put ("change", std::to_string (count.change));
	response_l.put ("state_v0", std::to_string (count.state_v0));
	response_l.put ("state_v1", std::to_string (count.state_v1));
	response_l.put ("state", std::to_string (count.state_v0 + count.state_v1));
	response_errors ();
}

void rai::rpc_handler::block_create ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string type (request.get<std::string> ("type"));
		rai::uint256_union wallet (0);
		boost::optional<std::string> wallet_text (request.get_optional<std::string> ("wallet"));
		if (wallet_text.is_initialized ())
		{
			if (wallet.decode_hex (wallet_text.get ()))
			{
				ec = nano::error_common::bad_wallet_number;
			}
		}
		rai::uint256_union account (0);
		boost::optional<std::string> account_text (request.get_optional<std::string> ("account"));
		if (!ec && account_text.is_initialized ())
		{
			if (account.decode_account (account_text.get ()))
			{
				ec = nano::error_common::bad_account_number;
			}
		}
		rai::uint256_union representative (0);
		boost::optional<std::string> representative_text (request.get_optional<std::string> ("representative"));
		if (!ec && representative_text.is_initialized ())
		{
			if (representative.decode_account (representative_text.get ()))
			{
				ec = nano::error_rpc::bad_representative_number;
			}
		}
		rai::uint256_union destination (0);
		boost::optional<std::string> destination_text (request.get_optional<std::string> ("destination"));
		if (!ec && destination_text.is_initialized ())
		{
			if (destination.decode_account (destination_text.get ()))
			{
				ec = nano::error_rpc::bad_destination;
			}
		}
		rai::block_hash source (0);
		boost::optional<std::string> source_text (request.get_optional<std::string> ("source"));
		if (!ec && source_text.is_initialized ())
		{
			if (source.decode_hex (source_text.get ()))
			{
				ec = nano::error_rpc::bad_source;
			}
		}
		rai::uint128_union amount (0);
		boost::optional<std::string> amount_text (request.get_optional<std::string> ("amount"));
		if (!ec && amount_text.is_initialized ())
		{
			if (amount.decode_dec (amount_text.get ()))
			{
				ec = nano::error_common::invalid_amount;
			}
		}
		auto work (work_optional_impl ());
		rai::raw_key prv;
		prv.data.clear ();
		rai::uint256_union previous (0);
		rai::uint128_union balance (0);
		if (!ec && wallet != 0 && account != 0)
		{
			auto existing (node.wallets.items.find (wallet));
			if (existing != node.wallets.items.end ())
			{
				auto transaction (node.store.tx_begin_read ());
				if (existing->second->store.valid_password (transaction))
				{
					if (existing->second->store.find (transaction, account) != existing->second->store.end ())
					{
						existing->second->store.fetch (transaction, account, prv);
						previous = node.ledger.latest (transaction, account);
						balance = node.ledger.account_balance (transaction, account);
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
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		boost::optional<std::string> key_text (request.get_optional<std::string> ("key"));
		if (!ec && key_text.is_initialized ())
		{
			if (prv.data.decode_hex (key_text.get ()))
			{
				ec = nano::error_common::bad_private_key;
			}
		}
		boost::optional<std::string> previous_text (request.get_optional<std::string> ("previous"));
		if (!ec && previous_text.is_initialized ())
		{
			if (previous.decode_hex (previous_text.get ()))
			{
				ec = nano::error_rpc::bad_previous;
			}
		}
		boost::optional<std::string> balance_text (request.get_optional<std::string> ("balance"));
		if (!ec && balance_text.is_initialized ())
		{
			if (balance.decode_dec (balance_text.get ()))
			{
				ec = nano::error_rpc::invalid_balance;
			}
		}
		rai::uint256_union link (0);
		boost::optional<std::string> link_text (request.get_optional<std::string> ("link"));
		if (!ec && link_text.is_initialized ())
		{
			if (link.decode_account (link_text.get ()))
			{
				if (link.decode_hex (link_text.get ()))
				{
					ec = nano::error_rpc::bad_link;
				}
			}
		}
		else
		{
			// Retrieve link from source or destination
			link = source.is_zero () ? destination : source;
		}
		if (prv.data != 0)
		{
			rai::uint256_union pub (rai::pub_key (prv.data));
			// Fetching account balance & previous for send blocks (if aren't given directly)
			if (!previous_text.is_initialized () && !balance_text.is_initialized ())
			{
				auto transaction (node.store.tx_begin_read ());
				previous = node.ledger.latest (transaction, pub);
				balance = node.ledger.account_balance (transaction, pub);
			}
			// Double check current balance if previous block is specified
			else if (previous_text.is_initialized () && balance_text.is_initialized () && type == "send")
			{
				auto transaction (node.store.tx_begin_read ());
				if (node.store.block_exists (transaction, previous) && node.store.block_balance (transaction, previous) != balance.number ())
				{
					ec = nano::error_rpc::block_create_balance_mismatch;
				}
			}
			// Check for incorrect account key
			if (!ec && account_text.is_initialized ())
			{
				if (account != pub)
				{
					ec = nano::error_rpc::block_create_public_key_mismatch;
				}
			}
			if (type == "state")
			{
				if (previous_text.is_initialized () && !representative.is_zero () && (!link.is_zero () || link_text.is_initialized ()))
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (previous.is_zero () ? pub : previous);
					}
					rai::state_block state (pub, previous, representative, balance, link, prv, pub, work);
					response_l.put ("hash", state.hash ().to_string ());
					std::string contents;
					state.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_state;
				}
			}
			else if (type == "open")
			{
				if (representative != 0 && source != 0)
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (pub);
					}
					rai::open_block open (source, representative, pub, prv, pub, work);
					response_l.put ("hash", open.hash ().to_string ());
					std::string contents;
					open.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_open;
				}
			}
			else if (type == "receive")
			{
				if (source != 0 && previous != 0)
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (previous);
					}
					rai::receive_block receive (previous, source, prv, pub, work);
					response_l.put ("hash", receive.hash ().to_string ());
					std::string contents;
					receive.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_receive;
				}
			}
			else if (type == "change")
			{
				if (representative != 0 && previous != 0)
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (previous);
					}
					rai::change_block change (previous, representative, prv, pub, work);
					response_l.put ("hash", change.hash ().to_string ());
					std::string contents;
					change.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_change;
				}
			}
			else if (type == "send")
			{
				if (destination != 0 && previous != 0 && balance != 0 && amount != 0)
				{
					if (balance.number () >= amount.number ())
					{
						if (work == 0)
						{
							work = node.work_generate_blocking (previous);
						}
						rai::send_block send (previous, destination, balance.number () - amount.number (), prv, pub, work);
						response_l.put ("hash", send.hash ().to_string ());
						std::string contents;
						send.serialize_json (contents);
						response_l.put ("block", contents);
					}
					else
					{
						ec = nano::error_common::insufficient_balance;
					}
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_send;
				}
			}
			else
			{
				ec = nano::error_blocks::invalid_type;
			}
		}
		else
		{
			ec = nano::error_rpc::block_create_key_required;
		}
	}
	response_errors ();
}

void rai::rpc_handler::block_hash ()
{
	std::string block_text (request.get<std::string> ("block"));
	boost::property_tree::ptree block_l;
	std::stringstream block_stream (block_text);
	boost::property_tree::read_json (block_stream, block_l);
	block_l.put ("signature", "0");
	block_l.put ("work", "0");
	auto block (rai::deserialize_block_json (block_l));
	if (block != nullptr)
	{
		response_l.put ("hash", block->hash ().to_string ());
	}
	else
	{
		ec = nano::error_blocks::invalid_block;
	}
	response_errors ();
}

void rai::rpc_handler::process ()
{
	std::string block_text (request.get<std::string> ("block"));
	boost::property_tree::ptree block_l;
	std::stringstream block_stream (block_text);
	boost::property_tree::read_json (block_stream, block_l);
	std::shared_ptr<rai::block> block (rai::deserialize_block_json (block_l));
	if (block != nullptr)
	{
		if (!rai::work_validate (*block))
		{
			auto hash (block->hash ());
			node.block_arrival.add (hash);
			rai::process_return result;
			{
				auto transaction (node.store.tx_begin_write ());
				result = node.block_processor.process_receive_one (transaction, block, std::chrono::steady_clock::time_point ());
			}
			switch (result.code)
			{
				case rai::process_result::progress:
				{
					response_l.put ("hash", hash.to_string ());
					break;
				}
				case rai::process_result::gap_previous:
				{
					ec = nano::error_process::gap_previous;
					break;
				}
				case rai::process_result::gap_source:
				{
					ec = nano::error_process::gap_source;
					break;
				}
				case rai::process_result::old:
				{
					ec = nano::error_process::old;
					break;
				}
				case rai::process_result::bad_signature:
				{
					ec = nano::error_process::bad_signature;
					break;
				}
				case rai::process_result::negative_spend:
				{
					// TODO once we get RPC versioning, this should be changed to "negative spend"
					ec = nano::error_process::negative_spend;
					break;
				}
				case rai::process_result::balance_mismatch:
				{
					ec = nano::error_process::balance_mismatch;
					break;
				}
				case rai::process_result::unreceivable:
				{
					ec = nano::error_process::unreceivable;
					break;
				}
				case rai::process_result::block_position:
				{
					ec = nano::error_process::block_position;
					break;
				}
				case rai::process_result::fork:
				{
					const bool force = request.get<bool> ("force", false);
					if (force && rpc.config.enable_control)
					{
						node.active.erase (*block);
						node.block_processor.force (block);
						response_l.put ("hash", hash.to_string ());
					}
					else
					{
						ec = nano::error_process::fork;
					}
					break;
				}
				default:
				{
					ec = nano::error_process::other;
					break;
				}
			}
		}
		else
		{
			ec = nano::error_blocks::work_low;
		}
	}
	else
	{
		ec = nano::error_blocks::invalid_block;
	}
	response_errors ();
}

void rai::rpc_handler::chain (bool successors)
{
	auto hash (hash_impl ("block"));
	auto count (count_impl ());
	if (!ec)
	{
		boost::property_tree::ptree blocks;
		auto transaction (node.store.tx_begin_read ());
		while (!hash.is_zero () && blocks.size () < count)
		{
			auto block_l (node.store.block_get (transaction, hash));
			if (block_l != nullptr)
			{
				boost::property_tree::ptree entry;
				entry.put ("", hash.to_string ());
				blocks.push_back (std::make_pair ("", entry));
				hash = successors ? node.store.block_successor (transaction, hash) : block_l->previous ();
			}
			else
			{
				hash.clear ();
			}
		}
		response_l.add_child ("blocks", blocks);
	}
	response_errors ();
}

void rai::rpc_handler::confirmation_history ()
{
	boost::property_tree::ptree elections;
	{
		std::lock_guard<std::mutex> lock (node.active.mutex);
		for (auto i (node.active.confirmed.begin ()), n (node.active.confirmed.end ()); i != n; ++i)
		{
			boost::property_tree::ptree election;
			election.put ("hash", i->winner->hash ().to_string ());
			election.put ("tally", i->tally.to_string_dec ());
			elections.push_back (std::make_pair ("", election));
		}
	}
	response_l.add_child ("confirmations", elections);
	response_errors ();
}

void rai::rpc_handler::unchecked ()
{
	auto count (count_optional_impl ());
	if (!ec)
	{
		boost::property_tree::ptree unchecked;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.unchecked_begin (transaction)), n (node.store.unchecked_end ()); i != n && unchecked.size () < count; ++i)
		{
			auto block (i->second);
			std::string contents;
			block->serialize_json (contents);
			unchecked.put (block->hash ().to_string (), contents);
		}
		response_l.add_child ("blocks", unchecked);
	}
	response_errors ();
}

void rai::rpc_handler::unchecked_clear ()
{
	rpc_control_impl ();
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		node.store.unchecked_clear (transaction);
		response_l.put ("success", "");
	}
	response_errors ();
}

void rai::rpc_handler::unchecked_get ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.unchecked_begin (transaction)), n (node.store.unchecked_end ()); i != n; ++i)
		{
			std::shared_ptr<rai::block> block (i->second);
			if (block->hash () == hash)
			{
				std::string contents;
				block->serialize_json (contents);
				response_l.put ("contents", contents);
				break;
			}
		}
		if (response_l.empty ())
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::unchecked_keys ()
{
	auto count (count_optional_impl ());
	rai::uint256_union key (0);
	boost::optional<std::string> hash_text (request.get_optional<std::string> ("key"));
	if (!ec && hash_text.is_initialized ())
	{
		if (key.decode_hex (hash_text.get ()))
		{
			ec = nano::error_rpc::bad_key;
		}
	}
	if (!ec)
	{
		boost::property_tree::ptree unchecked;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.unchecked_begin (transaction, key)), n (node.store.unchecked_end ()); i != n && unchecked.size () < count; ++i)
		{
			boost::property_tree::ptree entry;
			auto block (i->second);
			std::string contents;
			block->serialize_json (contents);
			entry.put ("key", rai::block_hash (i->first).to_string ());
			entry.put ("hash", block->hash ().to_string ());
			entry.put ("contents", contents);
			unchecked.push_back (std::make_pair ("", entry));
		}
		response_l.add_child ("unchecked", unchecked);
	}
	response_errors ();
}
