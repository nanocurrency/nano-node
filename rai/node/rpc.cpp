#include <rai/node/rpc.hpp>

#include <rai/node/node.hpp>
#include <boost/algorithm/string.hpp>

#include <ed25519-donna/ed25519.h>

rai::rpc_config::rpc_config () :
address (boost::asio::ip::address_v6::loopback ()),
port (rai::rpc::rpc_port),
enable_control (false),
frontier_request_limit (16384),
chain_request_limit (16384)
{
}

rai::rpc_config::rpc_config (bool enable_control_a) :
address (boost::asio::ip::address_v6::loopback ()),
port (rai::rpc::rpc_port),
enable_control (enable_control_a),
frontier_request_limit (16384),
chain_request_limit (16384)
{
}

void rai::rpc_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
    tree_a.put ("address", address.to_string ());
    tree_a.put ("port", std::to_string (port));
    tree_a.put ("enable_control", enable_control);
	tree_a.put ("frontier_request_limit", frontier_request_limit);
	tree_a.put ("chain_request_limit", chain_request_limit);
}

bool rai::rpc_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
    try
    {
		auto address_l (tree_a.get <std::string> ("address"));
		auto port_l (tree_a.get <std::string> ("port"));
		enable_control = tree_a.get <bool> ("enable_control");
		auto frontier_request_limit_l (tree_a.get <std::string> ("frontier_request_limit"));
		auto chain_request_limit_l (tree_a.get <std::string> ("chain_request_limit"));
		try
		{
			port = std::stoul (port_l);
			result = port > std::numeric_limits <uint16_t>::max ();
			frontier_request_limit = std::stoull (frontier_request_limit_l);
			chain_request_limit = std::stoull (chain_request_limit_l);
		}
		catch (std::logic_error const &)
		{
			result = true;
		}
		boost::system::error_code ec;
		address = boost::asio::ip::address_v6::from_string (address_l, ec);
		if (ec)
		{
			result = true;
		}
    }
    catch (std::runtime_error const &)
    {
        result = true;
    }
	return result;
}

rai::rpc::rpc (boost::asio::io_service & service_a, rai::node & node_a, rai::rpc_config const & config_a) :
server (service_a),
config (config_a),
node (node_a)
{
	server.handle ("/", [this] (nghttp2::asio_http2::server::request const & request_a, nghttp2::asio_http2::server::response const & response_a) { handle_connection (request_a, response_a); });
	node_a.observers.blocks.add ([this] (rai::block const & block_a, rai::account const & account_a, rai::amount const &)
	{
		observer_action (account_a);
	});
}

void rai::rpc::start ()
{
	boost::system::error_code ec;
    server.listen_and_serve (ec, config.address.to_string (), std::to_string (config.port), true);
	assert (!ec);
}

void rai::rpc::stop ()
{
    server.stop ();
}

rai::rpc_handler::rpc_handler (rai::rpc & rpc_a, nghttp2::asio_http2::server::response const & response_a) :
rpc (rpc_a),
response (response_a)
{
}

void rai::rpc::send_response (nghttp2::asio_http2::server::response const & response_a, boost::property_tree::ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
	auto body (ostream.str ());
	nghttp2::asio_http2::header_map headers;
	headers.insert (std::pair <std::string, nghttp2::asio_http2::header_value> ("content-type", { "application/json", false }));
	headers.insert (std::pair <std::string, nghttp2::asio_http2::header_value> ("content-length", { std::to_string (body.size ()), false }));
	headers.insert (std::pair <std::string, nghttp2::asio_http2::header_value> ("Access-Control-Allow-Origin",  { "*", false }));
	response_a.write_head (200, headers);
	response_a.end (body);
}

void rai::rpc::observer_action (rai::account const & account_a)
{
	std::shared_ptr <rai::payment_observer> observer;
	{
		std::lock_guard <std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			observer = existing->second;
		}
	}
	if (observer != nullptr)
	{
		observer->observe ();
	}
}

void rai::rpc::error_response (nghttp2::asio_http2::server::response const & response_a, std::string const & message_a)
{
	boost::property_tree::ptree response_l;
	response_l.put ("error", message_a);
	send_response (response_a, response_l);
}

void rai::rpc_handler::account_balance ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_account (account_text));
	if (!error)
	{
		auto balance (rpc.node.balance (account));
		boost::property_tree::ptree response_l;
		response_l.put ("balance", balance.convert_to <std::string> ());
		rpc.send_response (response, response_l);
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::account_create ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				rai::account new_key (existing->second->deterministic_insert ());
				if (!new_key.is_zero ())
				{
					boost::property_tree::ptree response_l;
					response_l.put ("account", new_key.to_account ());
					rpc.send_response (response, response_l);
				}
				else
				{
					rpc.error_response (response, "Wallet is locked");
				}
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad wallet number");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::account_list ()
{
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union wallet;
	auto error (wallet.decode_hex (wallet_text));
	if (!error)
	{
		auto existing (rpc.node.wallets.items.find (wallet));
		if (existing != rpc.node.wallets.items.end ())
		{
			boost::property_tree::ptree response_l;
			boost::property_tree::ptree accounts;
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			for (auto i (existing->second->store.begin (transaction)), j (existing->second->store.end ()); i != j; ++i)
			{
				boost::property_tree::ptree entry;
				entry.put ("", rai::uint256_union (i->first).to_account ());
				accounts.push_back (std::make_pair ("", entry));
			}
			response_l.add_child ("accounts", accounts);
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Wallet not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad wallet number");
	}
}

void rai::rpc_handler::account_move ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		std::string source_text (request.get <std::string> ("source"));
		auto accounts_text (request.get_child ("accounts"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				auto wallet (existing->second);
				rai::uint256_union source;
				auto error (source.decode_hex (source_text));
				if (!error)
				{
					auto existing (rpc.node.wallets.items.find (source));
					if (existing != rpc.node.wallets.items.end ())
					{
						auto source (existing->second);
						std::vector <rai::public_key> accounts;
						for (auto i (accounts_text.begin ()), n (accounts_text.end ()); i != n; ++i)
						{
							rai::public_key account;
							account.decode_hex (i->second.get <std::string> (""));
							accounts.push_back (account);
						}
						rai::transaction transaction (rpc.node.store.environment, nullptr, true);
						auto error (wallet->store.move (transaction, source->store, accounts));
						boost::property_tree::ptree response_l;
						response_l.put ("moved", error ? "0" : "1");
						rpc.send_response (response, response_l);
					}
					else
					{
						rpc.error_response (response, "Source not found");
					}
				}
				else
				{
					rpc.error_response (response, "Bad source number");
				}
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad wallet number");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::account_representative ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::account account;
	auto error (account.decode_account (account_text));
	if (!error)
	{
		rai::transaction transaction (rpc.node.store.environment, nullptr, false);
		rai::account_info info;
		auto error (rpc.node.store.account_get (transaction, account, info));
		if (!error)
		{
			auto block (rpc.node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			boost::property_tree::ptree response_l;
			response_l.put ("representative", block->representative ().to_account ());
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Account not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::account_representative_set ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				auto wallet (existing->second);
				std::string account_text (request.get <std::string> ("account"));
				rai::account account;
				auto error (account.decode_account (account_text));
				if (!error)
				{
					std::string representative_text (request.get <std::string> ("representative"));
					rai::account representative;
					auto error (representative.decode_account (representative_text));
					if (!error)
					{
						auto rpc_l (shared_from_this ());
						wallet->change_async (account, representative, [rpc_l] (std::unique_ptr <rai::block> block)
						{
							rai::block_hash hash (0);
							if (block != nullptr)
							{
								hash = block->hash ();
							}
							boost::property_tree::ptree response_l;
							response_l.put ("block", hash.to_string ());
							rpc_l->rpc.send_response (rpc_l->response, response_l);
						});
					}
				}
				else
				{
					rpc.error_response (response, "Bad account number");
				}
			}
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::account_weight ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_account (account_text));
	if (!error)
	{
		auto balance (rpc.node.weight (account));
		boost::property_tree::ptree response_l;
		response_l.put ("weight", balance.convert_to <std::string> ());
		rpc.send_response (response, response_l);
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::available_supply ()
{
	auto genesis_balance (rpc.node.balance (rai::genesis_account)); // Cold storage genesis
	auto landing_balance (rpc.node.balance (rai::account ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"))); // Active unavailable account
	auto faucet_balance (rpc.node.balance (rai::account ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"))); // Faucet account
	auto available (rai::genesis_amount - genesis_balance - landing_balance - faucet_balance);
	boost::property_tree::ptree response_l;
	response_l.put ("available", available.convert_to <std::string> ());
	rpc.send_response (response, response_l);
}

void rai::rpc_handler::block ()
{
	std::string hash_text (request.get <std::string> ("hash"));
	rai::uint256_union hash;
	auto error (hash.decode_hex (hash_text));
	if (!error)
	{
		rai::transaction transaction (rpc.node.store.environment, nullptr, false);
		auto block (rpc.node.store.block_get (transaction, hash));
		if (block != nullptr)
		{
			boost::property_tree::ptree response_l;
			std::string contents;
			block->serialize_json (contents);
			response_l.put ("contents", contents);
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Block not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad hash number");
	}
}

void rai::rpc_handler::block_account ()
{
	std::string hash_text (request.get <std::string> ("hash"));
	rai::block_hash hash;
	if (!hash.decode_hex (hash_text))
	{
		rai::transaction transaction (rpc.node.store.environment, nullptr, false);
		if (rpc.node.store.block_exists (transaction, hash))
		{
			boost::property_tree::ptree response_l;
			auto account (rpc.node.ledger.account (transaction, hash));
			response_l.put ("account", account.to_account ());
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Block not found");
		}
	}
	else
	{
		rpc.error_response (response, "Invalid block hash");
	}
}

void rai::rpc_handler::block_count ()
{
	rai::transaction transaction (rpc.node.store.environment, nullptr, false);
	auto size (rpc.node.store.block_count (transaction));
	boost::property_tree::ptree response_l;
	response_l.put ("count", std::to_string (size));
	rpc.send_response (response, response_l);
}

void rai::rpc_handler::chain ()
{
	std::string block_text (request.get <std::string> ("block"));
	std::string count_text (request.get <std::string> ("count"));
	rai::block_hash block;
	if (!block.decode_hex (block_text))
	{
		uint64_t count;
		if (!rpc.decode_unsigned (count_text, count))
		{
			boost::property_tree::ptree response_l;
			boost::property_tree::ptree blocks;
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			while (!block.is_zero () && blocks.size () < count)
			{
				auto block_l (rpc.node.store.block_get (transaction, block));
				if (block_l != nullptr)
				{
					boost::property_tree::ptree entry;
					entry.put ("", block.to_string ());
					blocks.push_back (std::make_pair ("", entry));
					block = block_l->previous ();
				}
				else
				{
					block.clear ();
				}
			}
			response_l.add_child ("blocks", blocks);
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Invalid count limit");
		}
	}
	else
	{
		rpc.error_response (response, "Invalid block hash");
	}
}

void rai::rpc_handler::frontiers ()
{
	std::string account_text (request.get <std::string> ("account"));
	std::string count_text (request.get <std::string> ("count"));
	rai::account start;
	if (!start.decode_account (account_text))
	{
		uint64_t count;
		if (!rpc.decode_unsigned (count_text, count))
		{
			boost::property_tree::ptree response_l;
			boost::property_tree::ptree frontiers;
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			for (auto i (rpc.node.store.latest_begin (transaction, start)), n (rpc.node.store.latest_end ()); i != n && frontiers.size () < count; ++i)
			{
				frontiers.put (rai::account (i->first).to_account (), rai::account_info (i->second).head.to_string ());
			}
			response_l.add_child ("frontiers", frontiers);
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Invalid count limit");
		}
	}
	else
	{
		rpc.error_response (response, "Invalid starting account");
	}
}

void rai::rpc_handler::frontier_count ()
{
	rai::transaction transaction (rpc.node.store.environment, nullptr, false);
	auto size (rpc.node.store.frontier_count (transaction));
	boost::property_tree::ptree response_l;
	response_l.put ("count", std::to_string (size));
	rpc.send_response (response, response_l);
}

namespace
{
class history_visitor : public rai::block_visitor
{
public:
	history_visitor (rai::rpc_handler & handler_a, rai::transaction & transaction_a, boost::property_tree::ptree & tree_a, rai::block_hash const & hash_a) :
	handler (handler_a),
	transaction (transaction_a),
	tree (tree_a),
	hash (hash_a)
	{
	}
	void send_block (rai::send_block const & block_a)
	{
		tree.put ("type", "send");
		auto account (block_a.hashables.destination.to_account ());
		tree.put ("account", account);
		auto amount (handler.rpc.node.ledger.amount (transaction, hash).convert_to <std::string> ());
		tree.put ("amount", amount);
	}
	void receive_block (rai::receive_block const & block_a)
	{
		tree.put ("type", "receive");
		auto account (handler.rpc.node.ledger.account (transaction, block_a.hashables.source).to_account ());
		tree.put ("account", account);
		auto amount (handler.rpc.node.ledger.amount (transaction, hash).convert_to <std::string> ());
		tree.put ("amount", amount);
	}
	void open_block (rai::open_block const & block_a)
	{
		// Report opens as a receive
		tree.put ("type", "receive");
		if (block_a.hashables.source != rai::genesis_account)
		{
			tree.put ("account", handler.rpc.node.ledger.account (transaction, block_a.hashables.source).to_account ());
			tree.put ("amount", handler.rpc.node.ledger.amount (transaction, hash).convert_to <std::string> ());
		}
		else
		{
			tree.put ("account", rai::genesis_account.to_account ());
			tree.put ("amount", rai::genesis_amount.convert_to <std::string> ());
		}
	}
	void change_block (rai::change_block const &)
	{
		// Don't report change blocks
	}
	rai::rpc_handler & handler;
	rai::transaction & transaction;
	boost::property_tree::ptree & tree;
	rai::block_hash const & hash;
};
}

void rai::rpc_handler::history ()
{
	std::string hash_text (request.get <std::string> ("hash"));
	std::string count_text (request.get <std::string> ("count"));
	rai::block_hash hash;
	if (!hash.decode_hex (hash_text))
	{
		uint64_t count;
		if (!rpc.decode_unsigned (count_text, count))
		{
			boost::property_tree::ptree response_l;
			boost::property_tree::ptree history;
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			auto block (rpc.node.store.block_get (transaction, hash));
			while (block != nullptr && count > 0)
			{
				boost::property_tree::ptree entry;
				history_visitor visitor (*this, transaction, entry, hash);
				block->visit (visitor);
				if (!entry.empty ())
				{
					entry.put ("hash", hash.to_string ());
					history.push_back (std::make_pair ("", entry));
				}
				hash = block->previous ();
				block = rpc.node.store.block_get (transaction, hash);
				--count;
			}
			response_l.add_child ("history", history);
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Invalid count limit");
		}
	}
	else
	{
		rpc.error_response (response, "Invalid block hash");
	}
}

void rai::rpc_handler::keepalive ()
{
	if (rpc.config.enable_control)
	{
		std::string address_text (request.get <std::string> ("address"));
		std::string port_text (request.get <std::string> ("port"));
		uint16_t port;
		if (!rai::parse_port (port_text, port))
		{
			rpc.node.keepalive (address_text, port);
			boost::property_tree::ptree response_l;
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Invalid port");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::mrai_from_raw ()
{
	std::string amount_text (request.get <std::string> ("amount"));
	rai::uint128_union amount;
	if (!amount.decode_dec (amount_text))
	{
		auto result (amount.number () / rai::Mrai_ratio);
		boost::property_tree::ptree response_l;
		response_l.put ("amount", result.convert_to <std::string> ());
		rpc.send_response (response, response_l);
	}
	else
	{
		rpc.error_response (response, "Bad amount number");
	}
}

void rai::rpc_handler::mrai_to_raw ()
{
	std::string amount_text (request.get <std::string> ("amount"));
	rai::uint128_union amount;
	if (!amount.decode_dec (amount_text))
	{
		auto result (amount.number () * rai::Mrai_ratio);
		if (result > amount.number ())
		{
			boost::property_tree::ptree response_l;
			response_l.put ("amount", result.convert_to <std::string> ());
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Amount too big");
		}
	}
	else
	{
		rpc.error_response (response, "Bad amount number");
	}
}

void rai::rpc_handler::krai_from_raw ()
{
	std::string amount_text (request.get <std::string> ("amount"));
	rai::uint128_union amount;
	if (!amount.decode_dec (amount_text))
	{
		auto result (amount.number () / rai::krai_ratio);
		boost::property_tree::ptree response_l;
		response_l.put ("amount", result.convert_to <std::string> ());
		rpc.send_response (response, response_l);
	}
	else
	{
		rpc.error_response (response, "Bad amount number");
	}
}

void rai::rpc_handler::krai_to_raw ()
{
	std::string amount_text (request.get <std::string> ("amount"));
	rai::uint128_union amount;
	if (!amount.decode_dec (amount_text))
	{
		auto result (amount.number () * rai::krai_ratio);
		if (result > amount.number ())
		{
			boost::property_tree::ptree response_l;
			response_l.put ("amount", result.convert_to <std::string> ());
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Amount too big");
		}
	}
	else
	{
		rpc.error_response (response, "Bad amount number");
	}
}

void rai::rpc_handler::password_change ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				rai::transaction transaction (rpc.node.store.environment, nullptr, true);
				boost::property_tree::ptree response_l;
				std::string password_text (request.get <std::string> ("password"));
				auto error (existing->second->store.rekey (transaction, password_text));
				response_l.put ("changed", error ? "0" : "1");
				rpc.send_response (response, response_l);
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad account number");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::password_enter ()
{
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union wallet;
	auto error (wallet.decode_hex (wallet_text));
	if (!error)
	{
		auto existing (rpc.node.wallets.items.find (wallet));
		if (existing != rpc.node.wallets.items.end ())
		{
			boost::property_tree::ptree response_l;
			std::string password_text (request.get <std::string> ("password"));
			auto error (existing->second->enter_password (password_text));
			response_l.put ("valid", error ? "0" : "1");
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Wallet not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::password_valid ()
{
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union wallet;
	auto error (wallet.decode_hex (wallet_text));
	if (!error)
	{
		auto existing (rpc.node.wallets.items.find (wallet));
		if (existing != rpc.node.wallets.items.end ())
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			boost::property_tree::ptree response_l;
			response_l.put ("valid", existing->second->store.valid_password (transaction) ? "1" : "0");
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Wallet not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::peers ()
{
	boost::property_tree::ptree response_l;
	boost::property_tree::ptree peers_l;
	auto peers_list (rpc.node.peers.list());
	for (auto i (peers_list.begin ()), n (peers_list.end ()); i != n; ++i)
	{
		boost::property_tree::ptree entry;
		std::stringstream text;
		text << i->endpoint;
		entry.put ("", text.str ());
		peers_l.push_back (std::make_pair ("", entry));
	}
	response_l.add_child ("peers", peers_l);
	rpc.send_response (response, response_l);
}

void rai::rpc_handler::payment_begin ()
{
	std::string id_text (request.get <std::string> ("wallet"));
	rai::uint256_union id;
	if (!id.decode_hex (id_text))
	{
		auto existing (rpc.node.wallets.items.find (id));
		if (existing != rpc.node.wallets.items.end ())
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, true);
			std::shared_ptr <rai::wallet> wallet (existing->second);
			if (wallet->store.valid_password (transaction))
			{
				rai::account account (0);
				do
				{
					auto existing (wallet->free_accounts.begin ());
					if (existing != wallet->free_accounts.end ())
					{
						account = *existing;
						wallet->free_accounts.erase (existing);
						if (wallet->store.find (transaction, account) == wallet->store.end ())
						{
							BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Transaction wallet %1% externally modified listing account %1% as free but no longer exists") % id.to_string () % account.to_account ());
							account.clear ();
						}
						else
						{
							if (!rpc.node.ledger.account_balance (transaction, account).is_zero ())
							{
								BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Skipping account %1% for use as a transaction account since it's balance isn't zero") % account.to_account ());
								account.clear ();
							}
						}
					}
					else
					{
						account = wallet->deterministic_insert (transaction);
						break;
					}
				} while (account.is_zero ());
				if (!account.is_zero ())
				{
					boost::property_tree::ptree response_l;
					response_l.put ("account", account.to_account ());
					rpc.send_response (response, response_l);
				}
				else
				{
					rpc.error_response (response, "Unable to create transaction account");
				}
			}
			else
			{
				rpc.error_response (response, "Wallet locked");
			}
		}
		else
		{
			rpc.error_response (response, "Unable to find wallets");
		}
	}
	else
	{
		rpc.error_response (response, "Bad wallet number");
	}
}

void rai::rpc_handler::payment_init ()
{
	std::string id_text (request.get <std::string> ("wallet"));
	rai::uint256_union id;
	if (!id.decode_hex (id_text))
	{
		rai::transaction transaction (rpc.node.store.environment, nullptr, true);
		auto existing (rpc.node.wallets.items.find (id));
		if (existing != rpc.node.wallets.items.end ())
		{
			auto wallet (existing->second);
			if (wallet->store.valid_password (transaction))
			{
				wallet->init_free_accounts (transaction);
				boost::property_tree::ptree response_l;
				response_l.put ("status", "Ready");
				rpc.send_response (response, response_l);
			}
			else
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "Transaction wallet locked");
				rpc.send_response (response, response_l);
			}
		}
		else
		{
			boost::property_tree::ptree response_l;
			response_l.put ("status", "Unable to find transaction wallet");
			rpc.send_response (response, response_l);
		}
	}
	else
	{
		rpc.error_response (response, "Bad transaction wallet number");
	}
}

void rai::rpc_handler::payment_end ()
{
	std::string id_text (request.get <std::string> ("wallet"));
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union id;
	if (!id.decode_hex (id_text))
	{
		rai::transaction transaction (rpc.node.store.environment, nullptr, false);
		auto existing (rpc.node.wallets.items.find (id));
		if (existing != rpc.node.wallets.items.end ())
		{
			auto wallet (existing->second);
			rai::account account;
			if (!account.decode_account (account_text))
			{
				auto existing (wallet->store.find (transaction, account));
				if (existing != wallet->store.end ())
				{
					if (rpc.node.ledger.account_balance (transaction, account).is_zero ())
					{
						wallet->free_accounts.insert (account);
						boost::property_tree::ptree response_l;
						rpc.send_response (response, response_l);
					}
					else
					{
						rpc.error_response (response, "Account has non-zero balance");
					}
				}
				else
				{
					rpc.error_response (response, "Account not in wallet");
				}
			}
			else
			{
				rpc.error_response (response, "Invalid account number");
			}
		}
		else
		{
			rpc.error_response (response, "Unable to find wallet");
		}
	}
	else
	{
		rpc.error_response (response, "Bad wallet number");
	}
}

void rai::rpc_handler::payment_wait ()
{
	std::string account_text (request.get <std::string> ("account"));
	std::string amount_text (request.get <std::string> ("amount"));
	std::string timeout_text (request.get <std::string> ("timeout"));
	rai::uint256_union account;
	if (!account.decode_account (account_text))
	{
		rai::uint128_union amount;
		if (!amount.decode_dec (amount_text))
		{
			uint64_t timeout;
			if (!rpc.decode_unsigned (timeout_text, timeout))
			{
				{
					auto observer (std::make_shared <rai::payment_observer> (response, rpc, account, amount));
					observer->start (timeout);
					std::lock_guard <std::mutex> lock (rpc.mutex);
					assert (rpc.payment_observers.find (account) == rpc.payment_observers.end ());
					rpc.payment_observers [account] = observer;
				}
				rpc.observer_action (account);
			}
			else
			{
				rpc.error_response (response, "Bad timeout number");
			}
		}
		else
		{
			rpc.error_response (response, "Bad amount number");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::process ()
{
	std::string block_text (request.get <std::string> ("block"));
	boost::property_tree::ptree block_l;
	std::stringstream block_stream (block_text);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (rai::deserialize_block_json (block_l));
	if (block != nullptr)
	{
		if (!rpc.node.work.work_validate (*block))
		{
			rpc.node.process_receive_republish (std::move (block), 0);
			boost::property_tree::ptree response_l;
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Block work is invalid");
		}
	}
	else
	{
		rpc.error_response (response, "Block is invalid");
	}
}

void rai::rpc_handler::rai_from_raw ()
{
	std::string amount_text (request.get <std::string> ("amount"));
	rai::uint128_union amount;
	if (!amount.decode_dec (amount_text))
	{
		auto result (amount.number () / rai::rai_ratio);
		boost::property_tree::ptree response_l;
		response_l.put ("amount", result.convert_to <std::string> ());
		rpc.send_response (response, response_l);
	}
	else
	{
		rpc.error_response (response, "Bad amount number");
	}
}

void rai::rpc_handler::rai_to_raw ()
{
	std::string amount_text (request.get <std::string> ("amount"));
	rai::uint128_union amount;
	if (!amount.decode_dec (amount_text))
	{
		auto result (amount.number () * rai::rai_ratio);
		if (result > amount.number ())
		{
			boost::property_tree::ptree response_l;
			response_l.put ("amount", result.convert_to <std::string> ());
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Amount too big");
		}
	}
	else
	{
		rpc.error_response (response, "Bad amount number");
	}
}

void rai::rpc_handler::search_pending ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				auto error (existing->second->search_pending ());
				boost::property_tree::ptree response_l;
				response_l.put ("started", !error);
				rpc.send_response (response, response_l);
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::send ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				std::string source_text (request.get <std::string> ("source"));
				rai::account source;
				auto error (source.decode_account (source_text));
				if (!error)
				{
					std::string destination_text (request.get <std::string> ("destination"));
					rai::account destination;
					auto error (destination.decode_account (destination_text));
					if (!error)
					{
						std::string amount_text (request.get <std::string> ("amount"));
						rai::amount amount;
						auto error (amount.decode_dec (amount_text));
						if (!error)
						{
							auto rpc_l (shared_from_this ());
							existing->second->send_async (source, destination, amount.number (), [rpc_l] (std::unique_ptr <rai::block> block_a)
							{
								rai::uint256_union hash (0);
								if (block_a != nullptr)
								{
									hash = block_a->hash ();
								}
								boost::property_tree::ptree response_l;
								response_l.put ("block", hash.to_string ());
								rpc_l->rpc.send_response (rpc_l->response, response_l);
							});
						}
						else
						{
							rpc.error_response (response, "Bad amount format");
						}
					}
					else
					{
						rpc.error_response (response, "Bad destination account");
					}
				}
				else
				{
					rpc.error_response (response, "Bad source account");
				}
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad wallet number");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::stop ()
{
	if (rpc.config.enable_control)
	{
		rpc.stop ();
		rpc.node.stop ();
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::version ()
{
	boost::property_tree::ptree response_l;
	response_l.put ("rpc_version", "1");
	response_l.put ("store_version", std::to_string (rpc.node.store_version ()));
	response_l.put ("node_vendor", boost::str (boost::format ("RaiBlocks %1%.%2%.%3%") % RAIBLOCKS_VERSION_MAJOR % RAIBLOCKS_VERSION_MINOR % RAIBLOCKS_VERSION_PATCH));
	rpc.send_response (response, response_l);
}

void rai::rpc_handler::validate_account_number ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_account (account_text));
	boost::property_tree::ptree response_l;
	response_l.put ("valid", error ? "0" : "1");
	rpc.send_response (response, response_l);
}

void rai::rpc_handler::wallet_add ()
{
	if (rpc.config.enable_control)
	{
		std::string key_text (request.get <std::string> ("key"));
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::raw_key key;
		auto error (key.data.decode_hex (key_text));
		if (!error)
		{
			rai::uint256_union wallet;
			auto error (wallet.decode_hex (wallet_text));
			if (!error)
			{
				auto existing (rpc.node.wallets.items.find (wallet));
				if (existing != rpc.node.wallets.items.end ())
				{
					auto pub (existing->second->insert_adhoc (key));
					if (!pub.is_zero ())
					{
						boost::property_tree::ptree response_l;
						response_l.put ("account", pub.to_account ());
						rpc.send_response (response, response_l);
					}
					else
					{
						rpc.error_response (response, "Wallet locked");
					}
				}
				else
				{
					rpc.error_response (response, "Wallet not found");
				}
			}
			else
			{
				rpc.error_response (response, "Bad wallet number");
			}
		}
		else
		{
			rpc.error_response (response, "Bad private key");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::wallet_contains ()
{
	std::string account_text (request.get <std::string> ("account"));
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union account;
	auto error (account.decode_account (account_text));
	if (!error)
	{
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				rai::transaction transaction (rpc.node.store.environment, nullptr, false);
				auto exists (existing->second->store.find (transaction, account) != existing->second->store.end ());
				boost::property_tree::ptree response_l;
				response_l.put ("exists", exists ? "1" : "0");
				rpc.send_response (response, response_l);
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad wallet number");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::wallet_create ()
{
	if (rpc.config.enable_control)
	{
		rai::keypair wallet_id;
		auto wallet (rpc.node.wallets.create (wallet_id.pub));
		boost::property_tree::ptree response_l;
		response_l.put ("wallet", wallet_id.pub.to_string ());
		rpc.send_response (response, response_l);
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::wallet_destroy ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				rpc.node.wallets.destroy (wallet);
				boost::property_tree::ptree response_l;
				rpc.send_response (response, response_l);
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad wallet number");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::wallet_export ()
{
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union wallet;
	auto error (wallet.decode_hex (wallet_text));
	if (!error)
	{
		auto existing (rpc.node.wallets.items.find (wallet));
		if (existing != rpc.node.wallets.items.end ())
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			std::string json;
			existing->second->store.serialize_json (transaction, json);
			boost::property_tree::ptree response_l;
			response_l.put ("json", json);
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Wallet not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::wallet_key_valid ()
{
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union wallet;
	auto error (wallet.decode_hex (wallet_text));
	if (!error)
	{
		auto existing (rpc.node.wallets.items.find (wallet));
		if (existing != rpc.node.wallets.items.end ())
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			auto valid (existing->second->store.valid_password (transaction));
			boost::property_tree::ptree response_l;
			response_l.put ("valid", valid ? "1" : "0");
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Wallet not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad wallet number");
	}
}

void rai::rpc_handler::wallet_representative ()
{
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union wallet;
	auto error (wallet.decode_hex (wallet_text));
	if (!error)
	{
		auto existing (rpc.node.wallets.items.find (wallet));
		if (existing != rpc.node.wallets.items.end ())
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			boost::property_tree::ptree response_l;
			response_l.put ("representative", existing->second->store.representative (transaction).to_account ());
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Wallet not found");
		}
	}
	else
	{
		rpc.error_response (response, "Bad account number");
	}
}

void rai::rpc_handler::wallet_representative_set ()
{
	if (rpc.config.enable_control)
	{
		std::string wallet_text (request.get <std::string> ("wallet"));
		rai::uint256_union wallet;
		auto error (wallet.decode_hex (wallet_text));
		if (!error)
		{
			auto existing (rpc.node.wallets.items.find (wallet));
			if (existing != rpc.node.wallets.items.end ())
			{
				std::string representative_text (request.get <std::string> ("representative"));
				rai::account representative;
				auto error (representative.decode_account (representative_text));
				if (!error)
				{
					rai::transaction transaction (rpc.node.store.environment, nullptr, true);
					existing->second->store.representative_set (transaction, representative);
					boost::property_tree::ptree response_l;
					response_l.put ("set", "1");
					rpc.send_response (response, response_l);
				}
				else
				{
					rpc.error_response (response, "Invalid account number");
				}
			}
			else
			{
				rpc.error_response (response, "Wallet not found");
			}
		}
		else
		{
			rpc.error_response (response, "Bad account number");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::work_generate ()
{
	if (rpc.config.enable_control)
	{
		std::string hash_text (request.get <std::string> ("hash"));
		rai::block_hash hash;
		auto error (hash.decode_hex (hash_text));
		if (!error)
		{
			auto work (rpc.node.work.generate_maybe (hash));
			if (work)
			{
				boost::property_tree::ptree response_l;
				response_l.put ("work", rai::to_string_hex (work.value ()));
				rpc.send_response (response, response_l);
			}
			else
			{
				rpc.error_response (response, "Cancelled");
			}
		}
		else
		{
			rpc.error_response (response, "Bad block hash");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc_handler::work_cancel ()
{
	if (rpc.config.enable_control)
	{
		std::string hash_text (request.get <std::string> ("hash"));
		rai::block_hash hash;
		auto error (hash.decode_hex (hash_text));
		if (!error)
		{
			rpc.node.work.cancel (hash);
			boost::property_tree::ptree response_l;
			rpc.send_response (response, response_l);
		}
		else
		{
			rpc.error_response (response, "Bad block hash");
		}
	}
	else
	{
		rpc.error_response (response, "RPC control is disabled");
	}
}

void rai::rpc::handle_connection (nghttp2::asio_http2::server::request const & request_a, nghttp2::asio_http2::server::response const & response_a)
{
	if (request_a.method () == "POST")
	{
		auto const & headers (request_a.header ());
		auto existing (std::find_if (headers.begin (), headers.end (), [] (nghttp2::asio_http2::header_map::value_type const & item_a)
		{
			return boost::iequals (item_a.first, "content-length");
		}));
		if (existing != headers.end ())
		{
			uint64_t length;
			if (!decode_unsigned (existing->second.value, length))
			{
				if (length < 16384)
				{
					auto handler (std::make_shared <rai::rpc_handler> (*this, response_a));
					handler->body.reserve (length);
					request_a.on_data ([handler] (uint8_t const * data_a, size_t size_a)
					{
						if (size_a != 0)
						{
							handler->body.append (reinterpret_cast <char const *> (data_a), size_a);
						}
						else
						{
							handler->process_request ();
						}
					});
				}
				else
				{
					BOOST_LOG (node.log) << boost::str (boost::format ("content-length is too large %1%") % length);
				}
			}
			else
			{
				BOOST_LOG (node.log) << "content-length isn't a number";
			}
		}
		else
		{
			BOOST_LOG (node.log) << "RPC request did not contain content-length header";
		}
	}
	else
	{
		error_response (response_a, "Can only POST requests");
	}
}

namespace
{
void reprocess_body (std::string & body, boost::property_tree::ptree & tree_a)
{
	std::stringstream stream;
	boost::property_tree::write_json (stream, tree_a);
	body = stream.str ();
}
}

void rai::rpc_handler::process_request ()
{
	try
	{
		std::stringstream istream (body);
		boost::property_tree::read_json (istream, request);
		std::string action (request.get <std::string> ("action"));
		if (action == "password_enter")
		{
			password_enter ();
			request.erase ("password");
			reprocess_body (body, request);
		}
		else if (action == "password_change")
		{
			password_change ();
			request.erase ("password");
			reprocess_body (body, request);
		}
		if (rpc.node.config.logging.log_rpc ())
		{
			BOOST_LOG (rpc.node.log) << body;
		}
		if (action == "account_balance")
		{
			account_balance ();
		}
		else if (action == "account_create")
		{
			account_create ();
		}
		else if (action == "account_list")
		{
			account_list ();
		}
		else if (action == "account_move")
		{
			account_move ();
		}
		else if (action == "account_representative")
		{
			account_representative ();
		}
		else if (action == "account_representative_set")
		{
			account_representative_set ();
		}
		else if (action == "account_weight")
		{
			account_weight ();
		}
		else if (action == "available_supply")
		{
			available_supply ();
		}
		else if (action == "block")
		{
			block ();
		}
		else if (action == "block_account")
		{
			block_account ();
		}
		else if (action == "block_count")
		{
			block_count ();
		}
		else if (action == "chain")
		{
			chain ();
		}
		else if (action == "frontiers")
		{
			frontiers ();
		}
		else if (action == "frontier_count")
		{
			frontier_count ();
		}
		else if (action == "history")
		{
			history ();
		}
		else if (action == "keepalive")
		{
			keepalive ();
		}
		else if (action == "krai_from_raw")
		{
			krai_from_raw ();
		}
		else if (action == "krai_to_raw")
		{
			krai_to_raw ();
		}
		else if (action == "mrai_from_raw")
		{
			mrai_from_raw ();
		}
		else if (action == "mrai_to_raw")
		{
			mrai_to_raw ();
		}
		else if (action == "password_change")
		{
			// Processed before logging
		}
		else if (action == "password_enter")
		{
			// Processed before logging
		}
		else if (action == "password_valid")
		{
			password_valid ();
		}
		else if (action == "payment_begin")
		{
			payment_begin ();
		}
		else if (action == "payment_init")
		{
			payment_init ();
		}
		else if (action == "payment_end")
		{
			payment_end ();
		}
		else if (action == "payment_wait")
		{
			payment_wait ();
		}
		else if (action == "peers")
		{
			peers ();
		}
		else if (action == "process")
		{
			process ();
		}
		else if (action == "rai_from_raw")
		{
			rai_from_raw ();
		}
		else if (action == "rai_to_raw")
		{
			rai_to_raw ();
		}
		else if (action == "search_pending")
		{
			search_pending ();
		}
		else if (action == "send")
		{
			send ();
		}
		else if (action == "stop")
		{
			stop ();
		}
		else if (action == "validate_account_number")
		{
			validate_account_number ();
		}
		else if (action == "version")
		{
			version ();
		}
		else if (action == "wallet_add")
		{
			wallet_add ();
		}
		else if (action == "wallet_contains")
		{
			wallet_contains ();
		}
		else if (action == "wallet_create")
		{
			wallet_create ();
		}
		else if (action == "wallet_destroy")
		{
			wallet_destroy ();
		}
		else if (action == "wallet_export")
		{
			wallet_export ();
		}
		else if (action == "wallet_key_valid")
		{
			wallet_key_valid ();
		}
		else if (action == "wallet_representative")
		{
			wallet_representative ();
		}
		else if (action == "wallet_representative_set")
		{
			wallet_representative_set ();
		}
		else if (action == "work_generate")
		{
			work_generate ();
		}
		else if (action == "work_cancel")
		{
			work_cancel ();
		}
		else
		{
			rpc.error_response (response, "Unknown command");
		}
	}
	catch (std::runtime_error const & err)
	{
		rpc.error_response (response, "Unable to parse JSON");
	}
	catch (...)
	{
		rpc.error_response (response, "Internal server error in RPC");
	}
}

bool rai::rpc::decode_unsigned (std::string const & text, uint64_t & number)
{
	bool result;
	size_t end;
	try
	{
		number = std::stoull (text, &end);
		result = false;
	}
	catch (std::invalid_argument const &)
	{
		result = true;
	}
	catch (std::out_of_range const &)
	{
		result = true;
	}
	result = result || end != text.size ();
	return result;
}

rai::payment_observer::payment_observer (nghttp2::asio_http2::server::response const & response_a, rai::rpc & rpc_a, rai::account const & account_a, rai::amount const & amount_a) :
rpc (rpc_a),
account (account_a),
amount (amount_a),
response (response_a)
{
	completed.clear ();
}

void rai::payment_observer::start (uint64_t timeout)
{
	auto this_l (shared_from_this ());
	rpc.node.alarm.add (std::chrono::system_clock::now () + std::chrono::milliseconds (timeout), [this_l] ()
	{
		this_l->complete (rai::payment_status::nothing);
	});
}

rai::payment_observer::~payment_observer ()
{
}

void rai::payment_observer::observe ()
{
	if (rpc.node.balance (account) >= amount.number ())
	{
		complete (rai::payment_status::success);
	}
}

void rai::payment_observer::complete (rai::payment_status status)
{
	auto already (completed.test_and_set ());
	if (!already)
	{
		if (rpc.node.config.logging.log_rpc ())
		{
			BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Exiting payment_observer for account %1% status %2%") % account.to_account () % static_cast <unsigned> (status));
		}
		switch (status)
		{
			case rai::payment_status::nothing:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "nothing");
				rpc.send_response (response, response_l);
				break;
			}
			case rai::payment_status::success:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "success");
				rpc.send_response (response, response_l);
				break;
			}
			default:
			{
				rpc.error_response (response, "Internal payment error");
				break;
			}
		}
		std::lock_guard <std::mutex> lock (rpc.mutex);
		assert (rpc.payment_observers.find (account) != rpc.payment_observers.end ());
		rpc.payment_observers.erase (account);
	}
}
