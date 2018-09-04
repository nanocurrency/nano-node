#include <rai/lib/errors.hpp>
#include <rai/lib/interface.h>
#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

namespace
{
class history_visitor : public rai::block_visitor
{
public:
	history_visitor (rai::rpc_handler & handler_a, bool raw_a, rai::transaction & transaction_a, boost::property_tree::ptree & tree_a, rai::block_hash const & hash_a) :
	handler (handler_a),
	raw (raw_a),
	transaction (transaction_a),
	tree (tree_a),
	hash (hash_a)
	{
	}
	virtual ~history_visitor () = default;
	void send_block (rai::send_block const & block_a)
	{
		tree.put ("type", "send");
		auto account (block_a.hashables.destination.to_account ());
		tree.put ("account", account);
		auto amount (handler.node.ledger.amount (transaction, hash).convert_to<std::string> ());
		tree.put ("amount", amount);
		if (raw)
		{
			tree.put ("destination", account);
			tree.put ("balance", block_a.hashables.balance.to_string_dec ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void receive_block (rai::receive_block const & block_a)
	{
		tree.put ("type", "receive");
		auto account (handler.node.ledger.account (transaction, block_a.hashables.source).to_account ());
		tree.put ("account", account);
		auto amount (handler.node.ledger.amount (transaction, hash).convert_to<std::string> ());
		tree.put ("amount", amount);
		if (raw)
		{
			tree.put ("source", block_a.hashables.source.to_string ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void open_block (rai::open_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "open");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("source", block_a.hashables.source.to_string ());
			tree.put ("opened", block_a.hashables.account.to_account ());
		}
		else
		{
			// Report opens as a receive
			tree.put ("type", "receive");
		}
		if (block_a.hashables.source != rai::genesis_account)
		{
			tree.put ("account", handler.node.ledger.account (transaction, block_a.hashables.source).to_account ());
			tree.put ("amount", handler.node.ledger.amount (transaction, hash).convert_to<std::string> ());
		}
		else
		{
			tree.put ("account", rai::genesis_account.to_account ());
			tree.put ("amount", rai::genesis_amount.convert_to<std::string> ());
		}
	}
	void change_block (rai::change_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "change");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void state_block (rai::state_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "state");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("link", block_a.hashables.link.to_string ());
			tree.put ("balance", block_a.hashables.balance.to_string_dec ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
		auto balance (block_a.hashables.balance.number ());
		auto previous_balance (handler.node.ledger.balance (transaction, block_a.hashables.previous));
		if (balance < previous_balance)
		{
			if (raw)
			{
				tree.put ("subtype", "send");
			}
			else
			{
				tree.put ("type", "send");
			}
			tree.put ("account", block_a.hashables.link.to_account ());
			tree.put ("amount", (previous_balance - balance).convert_to<std::string> ());
		}
		else
		{
			if (block_a.hashables.link.is_zero ())
			{
				if (raw)
				{
					tree.put ("subtype", "change");
				}
			}
			else if (balance == previous_balance && !handler.node.ledger.epoch_link.is_zero () && block_a.hashables.link == handler.node.ledger.epoch_link)
			{
				if (raw)
				{
					tree.put ("subtype", "epoch");
					tree.put ("account", handler.node.ledger.epoch_signer.to_account ());
				}
			}
			else
			{
				if (raw)
				{
					tree.put ("subtype", "receive");
				}
				else
				{
					tree.put ("type", "receive");
				}
				tree.put ("account", handler.node.ledger.account (transaction, block_a.hashables.link).to_account ());
				tree.put ("amount", (balance - previous_balance).convert_to<std::string> ());
			}
		}
	}
	rai::rpc_handler & handler;
	bool raw;
	rai::transaction & transaction;
	boost::property_tree::ptree & tree;
	rai::block_hash const & hash;
};
}

rai::account rai::rpc_handler::account_impl (std::string account_text)
{
	rai::account result (0);
	if (!ec)
	{
		if (account_text.empty ())
		{
			account_text = request.get<std::string> ("account");
		}
		if (result.decode_account (account_text))
		{
			ec = nano::error_common::bad_account_number;
		}
	}
	return result;
}

void rai::rpc_handler::delegators ()
{
	auto account (account_impl ());
	if (!ec)
	{
		boost::property_tree::ptree delegators;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.latest_begin (transaction)), n (node.store.latest_end ()); i != n; ++i)
		{
			rai::account_info info (i->second);
			auto block (node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			if (block->representative () == account)
			{
				std::string balance;
				rai::uint128_union (info.balance).encode_dec (balance);
				delegators.put (rai::account (i->first).to_account (), balance);
			}
		}
		response_l.add_child ("delegators", delegators);
	}
	response_errors ();
}

void rai::rpc_handler::delegators_count ()
{
	auto account (account_impl ());
	if (!ec)
	{
		uint64_t count (0);
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.latest_begin (transaction)), n (node.store.latest_end ()); i != n; ++i)
		{
			rai::account_info info (i->second);
			auto block (node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			if (block->representative () == account)
			{
				++count;
			}
		}
		response_l.put ("count", std::to_string (count));
	}
	response_errors ();
}

void rai::rpc_handler::frontiers ()
{
	auto start (account_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		boost::property_tree::ptree frontiers;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && frontiers.size () < count; ++i)
		{
			frontiers.put (rai::account (i->first).to_account (), rai::account_info (i->second).head.to_string ());
		}
		response_l.add_child ("frontiers", frontiers);
	}
	response_errors ();
}

void rai::rpc_handler::account_count ()
{
	auto transaction (node.store.tx_begin_read ());
	auto size (node.store.account_count (transaction));
	response_l.put ("count", std::to_string (size));
	response_errors ();
}

void rai::rpc_handler::account_history ()
{
	rai::account account;
	bool output_raw (request.get_optional<bool> ("raw") == true);
	rai::block_hash hash;
	auto head_str (request.get_optional<std::string> ("head"));
	auto transaction (node.store.tx_begin_read ());
	if (head_str)
	{
		if (!hash.decode_hex (*head_str))
		{
			account = node.ledger.account (transaction, hash);
		}
		else
		{
			ec = nano::error_blocks::bad_hash_number;
		}
	}
	else
	{
		account = account_impl ();
		if (!ec)
		{
			hash = node.ledger.latest (transaction, account);
		}
	}
	auto count (count_impl ());
	if (!ec)
	{
		uint64_t offset = 0;
		auto offset_text (request.get_optional<std::string> ("offset"));
		if (!offset_text || !rai::decode_unsigned (*offset_text, offset))
		{
			boost::property_tree::ptree history;
			response_l.put ("account", account.to_account ());
			auto block (node.store.block_get (transaction, hash));
			while (block != nullptr && count > 0)
			{
				if (offset > 0)
				{
					--offset;
				}
				else
				{
					boost::property_tree::ptree entry;
					history_visitor visitor (*this, output_raw, transaction, entry, hash);
					block->visit (visitor);
					if (!entry.empty ())
					{
						entry.put ("hash", hash.to_string ());
						if (output_raw)
						{
							entry.put ("work", rai::to_string_hex (block->block_work ()));
							entry.put ("signature", block->block_signature ().to_string ());
						}
						history.push_back (std::make_pair ("", entry));
					}
					--count;
				}
				hash = block->previous ();
				block = node.store.block_get (transaction, hash);
			}
			response_l.add_child ("history", history);
			if (!hash.is_zero ())
			{
				response_l.put ("previous", hash.to_string ());
			}
		}
		else
		{
			ec = nano::error_rpc::invalid_offset;
		}
	}
	response_errors ();
}

void rai::rpc_handler::account_balance ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto balance (node.balance_pending (account));
		response_l.put ("balance", balance.first.convert_to<std::string> ());
		response_l.put ("pending", balance.second.convert_to<std::string> ());
	}
	response_errors ();
}

void rai::rpc_handler::account_block_count ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		rai::account_info info;
		if (!node.store.account_get (transaction, account, info))
		{
			response_l.put ("block_count", std::to_string (info.block_count));
		}
		else
		{
			ec = nano::error_common::account_not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::account_create ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		const bool generate_work = request.get<bool> ("work", true);
		rai::account new_key (wallet->deterministic_insert (generate_work));
		if (!new_key.is_zero ())
		{
			response_l.put ("account", new_key.to_account ());
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	response_errors ();
}

void rai::rpc_handler::account_get ()
{
	std::string key_text (request.get<std::string> ("key"));
	rai::uint256_union pub;
	if (!pub.decode_hex (key_text))
	{
		response_l.put ("account", pub.to_account ());
	}
	else
	{
		ec = nano::error_common::bad_public_key;
	}
	response_errors ();
}

void rai::rpc_handler::account_info ()
{
	auto account (account_impl ());
	if (!ec)
	{
		const bool representative = request.get<bool> ("representative", false);
		const bool weight = request.get<bool> ("weight", false);
		const bool pending = request.get<bool> ("pending", false);
		auto transaction (node.store.tx_begin_read ());
		rai::account_info info;
		if (!node.store.account_get (transaction, account, info))
		{
			response_l.put ("frontier", info.head.to_string ());
			response_l.put ("open_block", info.open_block.to_string ());
			response_l.put ("representative_block", info.rep_block.to_string ());
			std::string balance;
			rai::uint128_union (info.balance).encode_dec (balance);
			response_l.put ("balance", balance);
			response_l.put ("modified_timestamp", std::to_string (info.modified));
			response_l.put ("block_count", std::to_string (info.block_count));
			response_l.put ("account_version", info.epoch == rai::epoch::epoch_1 ? "1" : "0");
			if (representative)
			{
				auto block (node.store.block_get (transaction, info.rep_block));
				assert (block != nullptr);
				response_l.put ("representative", block->representative ().to_account ());
			}
			if (weight)
			{
				auto account_weight (node.ledger.weight (transaction, account));
				response_l.put ("weight", account_weight.convert_to<std::string> ());
			}
			if (pending)
			{
				auto account_pending (node.ledger.account_pending (transaction, account));
				response_l.put ("pending", account_pending.convert_to<std::string> ());
			}
		}
		else
		{
			ec = nano::error_common::account_not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::account_key ()
{
	auto account (account_impl ());
	if (!ec)
	{
		response_l.put ("key", account.to_string ());
	}
	response_errors ();
}

void rai::rpc_handler::account_list ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree accounts;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), j (wallet->store.end ()); i != j; ++i)
		{
			boost::property_tree::ptree entry;
			entry.put ("", rai::account (i->first).to_account ());
			accounts.push_back (std::make_pair ("", entry));
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void rai::rpc_handler::account_move ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string source_text (request.get<std::string> ("source"));
		auto accounts_text (request.get_child ("accounts"));
		rai::uint256_union source;
		if (!source.decode_hex (source_text))
		{
			auto existing (node.wallets.items.find (source));
			if (existing != node.wallets.items.end ())
			{
				auto source (existing->second);
				std::vector<rai::public_key> accounts;
				for (auto i (accounts_text.begin ()), n (accounts_text.end ()); i != n; ++i)
				{
					rai::public_key account;
					account.decode_hex (i->second.get<std::string> (""));
					accounts.push_back (account);
				}
				auto transaction (node.store.tx_begin_write ());
				auto error (wallet->store.move (transaction, source->store, accounts));
				response_l.put ("moved", error ? "0" : "1");
			}
			else
			{
				ec = nano::error_rpc::source_not_found;
			}
		}
		else
		{
			ec = nano::error_rpc::bad_source;
		}
	}
	response_errors ();
}

void rai::rpc_handler::account_remove ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		if (wallet->store.valid_password (transaction))
		{
			if (wallet->store.find (transaction, account) != wallet->store.end ())
			{
				wallet->store.erase (transaction, account);
				response_l.put ("removed", "1");
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
	response_errors ();
}

void rai::rpc_handler::account_representative ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		rai::account_info info;
		if (!node.store.account_get (transaction, account, info))
		{
			auto block (node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			response_l.put ("representative", block->representative ().to_account ());
		}
		else
		{
			ec = nano::error_common::account_not_found;
		}
	}
	response_errors ();
}

void rai::rpc_handler::account_representative_set ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		std::string representative_text (request.get<std::string> ("representative"));
		rai::account representative;
		if (!representative.decode_account (representative_text))
		{
			auto work (work_optional_impl ());
			if (!ec && work)
			{
				auto transaction (node.store.tx_begin_write ());
				if (wallet->store.valid_password (transaction))
				{
					rai::account_info info;
					if (!node.store.account_get (transaction, account, info))
					{
						if (!rai::work_validate (info.head, work))
						{
							wallet->store.work_put (transaction, account, work);
						}
						else
						{
							ec = nano::error_common::invalid_work;
						}
					}
					else
					{
						ec = nano::error_common::account_not_found;
					}
				}
				else
				{
					ec = nano::error_common::wallet_locked;
				}
			}
			if (!ec)
			{
				auto response_a (response);
				wallet->change_async (account, representative, [response_a](std::shared_ptr<rai::block> block) {
					rai::block_hash hash (0);
					if (block != nullptr)
					{
						hash = block->hash ();
					}
					boost::property_tree::ptree response_l;
					response_l.put ("block", hash.to_string ());
					response_a (response_l);
				},
				work == 0);
			}
		}
		else
		{
			ec = nano::error_rpc::bad_representative_number;
		}
	}
	// Because of change_async
	if (ec)
	{
		response_errors ();
	}
}

void rai::rpc_handler::account_weight ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto balance (node.weight (account));
		response_l.put ("weight", balance.convert_to<std::string> ());
	}
	response_errors ();
}

void rai::rpc_handler::accounts_balances ()
{
	boost::property_tree::ptree balances;
	for (auto & accounts : request.get_child ("accounts"))
	{
		auto account (account_impl (accounts.second.data ()));
		if (!ec)
		{
			boost::property_tree::ptree entry;
			auto balance (node.balance_pending (account));
			entry.put ("balance", balance.first.convert_to<std::string> ());
			entry.put ("pending", balance.second.convert_to<std::string> ());
			balances.push_back (std::make_pair (account.to_account (), entry));
		}
	}
	response_l.add_child ("balances", balances);
	response_errors ();
}

void rai::rpc_handler::accounts_create ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		const bool generate_work = request.get<bool> ("work", false);
		boost::property_tree::ptree accounts;
		for (auto i (0); accounts.size () < count; ++i)
		{
			rai::account new_key (wallet->deterministic_insert (generate_work));
			if (!new_key.is_zero ())
			{
				boost::property_tree::ptree entry;
				entry.put ("", new_key.to_account ());
				accounts.push_back (std::make_pair ("", entry));
			}
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void rai::rpc_handler::accounts_frontiers ()
{
	boost::property_tree::ptree frontiers;
	auto transaction (node.store.tx_begin_read ());
	for (auto & accounts : request.get_child ("accounts"))
	{
		auto account (account_impl (accounts.second.data ()));
		if (!ec)
		{
			auto latest (node.ledger.latest (transaction, account));
			if (!latest.is_zero ())
			{
				frontiers.put (account.to_account (), latest.to_string ());
			}
		}
	}
	response_l.add_child ("frontiers", frontiers);
	response_errors ();
}

void rai::rpc_handler::accounts_pending ()
{
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	const bool source = request.get<bool> ("source", false);
	const bool include_active = request.get<bool> ("include_active", false);
	boost::property_tree::ptree pending;
	auto transaction (node.store.tx_begin_read ());
	for (auto & accounts : request.get_child ("accounts"))
	{
		auto account (account_impl (accounts.second.data ()));
		if (!ec)
		{
			boost::property_tree::ptree peers_l;
			rai::account end (account.number () + 1);
			for (auto i (node.store.pending_begin (transaction, rai::pending_key (account, 0))), n (node.store.pending_begin (transaction, rai::pending_key (end, 0))); i != n && peers_l.size () < count; ++i)
			{
				rai::pending_key key (i->first);
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
						rai::pending_info info (i->second);
						if (info.amount.number () >= threshold.number ())
						{
							if (source)
							{
								boost::property_tree::ptree pending_tree;
								pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
								pending_tree.put ("source", info.source.to_account ());
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
			pending.add_child (account.to_account (), peers_l);
		}
	}
	response_l.add_child ("blocks", pending);
	response_errors ();
}

void rai::rpc_handler::pending ()
{
	auto account (account_impl ());
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	const bool source = request.get<bool> ("source", false);
	const bool min_version = request.get<bool> ("min_version", false);
	if (!ec)
	{
		boost::property_tree::ptree peers_l;
		auto transaction (node.store.tx_begin_read ());
		rai::account end (account.number () + 1);
		for (auto i (node.store.pending_begin (transaction, rai::pending_key (account, 0))), n (node.store.pending_begin (transaction, rai::pending_key (end, 0))); i != n && peers_l.size () < count; ++i)
		{
			rai::pending_key key (i->first);
			if (threshold.is_zero () && !source && !min_version)
			{
				boost::property_tree::ptree entry;
				entry.put ("", key.hash.to_string ());
				peers_l.push_back (std::make_pair ("", entry));
			}
			else
			{
				rai::pending_info info (i->second);
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
		response_l.add_child ("blocks", peers_l);
	}
	response_errors ();
}

void rai::rpc_handler::pending_exists ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto block (node.store.block_get (transaction, hash));
		if (block != nullptr)
		{
			auto exists (false);
			auto destination (node.ledger.block_destination (transaction, *block));
			if (!destination.is_zero ())
			{
				exists = node.store.pending_exists (transaction, rai::pending_key (destination, hash));
			}
			response_l.put ("exists", exists ? "1" : "0");
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}
