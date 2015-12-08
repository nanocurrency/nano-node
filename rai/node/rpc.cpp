#include <rai/node/rpc.hpp>

#include <rai/node/node.hpp>
#include <boost/network/uri/uri.ipp>

#include <ed25519-donna/ed25519.h>

rai::rpc_config::rpc_config () :
address (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ())),
port (rai::network::rpc_port),
enable_control (false),
frontier_request_limit (16384),
chain_request_limit (16384)
{
}

rai::rpc_config::rpc_config (bool enable_control_a) :
address (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ())),
port (rai::network::rpc_port),
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

rai::rpc::rpc (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, rai::node & node_a, rai::rpc_config const & config_a) :
config (config_a),
server (decltype (server)::options (*this).address (config.address.to_string ()).port (std::to_string (config.port)).io_service (service_a).thread_pool (pool_a).reuse_address (true)),
node (node_a)
{
	node_a.observers.push_back ([this] (rai::block const & block_a, rai::account const & account_a)
	{
		std::lock_guard <std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			existing->second ();
		}
	});
}

void rai::rpc::start ()
{
    server.listen ();
}

void rai::rpc::stop ()
{
    server.stop ();
}

rai::rpc_handler::rpc_handler (rai::rpc & rpc_a, size_t length_a, boost::network::http::async_server <rai::rpc>::request const & headers_a, boost::network::http::async_server <rai::rpc>::connection_ptr connection_a) :
length (length_a),
rpc (rpc_a),
headers (headers_a),
connection (connection_a)
{
}

void rai::rpc_handler::send_response (boost::property_tree::ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    auto response_l (boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::ok));
	auto response (std::make_shared <decltype (response_l)> (response_l));
	response->headers.clear ();
    response->headers.push_back (boost::network::http::response_header_narrow {"content-type", "application/json"});
    response->content = ostream.str ();
	response->headers.push_back (boost::network::http::response_header_narrow {"content-length", std::to_string (response->content.size ())});
	connection->write (response->to_buffers (), [response] (boost::system::error_code const & ec)
	{
	});
}

void rai::rpc_handler::account_balance ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_base58check (account_text));
	if (!error)
	{
		auto balance (rpc.node.balance (account));
		boost::property_tree::ptree response_l;
		response_l.put ("balance", balance.convert_to <std::string> ());
		send_response (response_l);
	}
	else
	{
		error_response ("Bad account number");
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
				rai::keypair new_key;
				existing->second->insert (new_key.prv);
				boost::property_tree::ptree response_l;
				response_l.put ("account", new_key.pub.to_base58check ());
				send_response (response_l);
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad wallet number");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
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
				entry.put ("", rai::uint256_union (i->first).to_base58check ());
				accounts.push_back (std::make_pair ("", entry));
			}
			response_l.add_child ("accounts", accounts);
			send_response (response_l);
		}
		else
		{
			error_response ("Wallet not found");
		}
	}
	else
	{
		error_response ("Bad wallet number");
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
						send_response (response_l);
					}
					else
					{
						error_response ("Source not found");
					}
				}
				else
				{
					error_response ("Bad source number");
				}
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad wallet number");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
	}
}

void rai::rpc_handler::account_weight ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_base58check (account_text));
	if (!error)
	{
		auto balance (rpc.node.weight (account));
		boost::property_tree::ptree response_l;
		response_l.put ("weight", balance.convert_to <std::string> ());
		send_response (response_l);
	}
	else
	{
		error_response ("Bad account number");
	}
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
			send_response (response_l);
		}
		else
		{
			error_response ("Block not found");
		}
	}
	else
	{
		error_response ("Bad hash number");
	}
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
			send_response (response_l);
		}
		else
		{
			error_response ("Invalid count limit");
		}
	}
	else
	{
		error_response ("Invalid block hash");
	}
}

void rai::rpc_handler::frontiers ()
{
	std::string account_text (request.get <std::string> ("account"));
	std::string count_text (request.get <std::string> ("count"));
	rai::account start;
	if (!start.decode_base58check (account_text))
	{
		uint64_t count;
		if (!rpc.decode_unsigned (count_text, count))
		{
			boost::property_tree::ptree response_l;
			boost::property_tree::ptree frontiers;
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			for (auto i (rpc.node.store.latest_begin (transaction, start)), n (rpc.node.store.latest_end ()); i != n && frontiers.size () < count; ++i)
			{
				frontiers.put (rai::account (i->first).to_base58check (), rai::account_info (i->second).head.to_string ());
			}
			response_l.add_child ("frontiers", frontiers);
			send_response (response_l);
		}
		else
		{
			error_response ("Invalid count limit");
		}
	}
	else
	{
		error_response ("Invalid starting account");
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
			send_response (response_l);
		}
		else
		{
			error_response ("Invalid port");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
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
				send_response (response_l);
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad account number");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
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
			send_response (response_l);
		}
		else
		{
			error_response ("Wallet not found");
		}
	}
	else
	{
		error_response ("Bad account number");
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
			send_response (response_l);
		}
		else
		{
			error_response ("Wallet not found");
		}
	}
	else
	{
		error_response ("Bad account number");
	}
}

void rai::rpc_handler::price ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_base58check (account_text));
	if (!error)
	{
		auto amount_text (request.get <std::string> ("amount"));
		uint64_t amount;
		if (!rpc.decode_unsigned (amount_text, amount))
		{
			if (amount <= 1000)
			{
				auto balance (rpc.node.balance (account));
				if (balance >= amount * rai::Grai_ratio)
				{
					auto price (rpc.node.price (balance, amount));
					boost::property_tree::ptree response_l;
					response_l.put ("price", std::to_string (price));
					send_response (response_l);
				}
				else
				{
					error_response ("Requesting more blocks than are available");
				}
			}
			else
			{
				error_response ("Cannot purchase more than 1000");
			}
		}
		else
		{
			error_response ("Invalid amount");
		}
	}
	else
	{
		error_response ("Bad account number");
	}
}

void rai::rpc_handler::payment_begin ()
{
	std::string inactive_text (request.get <std::string> ("inactive_wallet"));
	std::string active_text (request.get <std::string> ("active_wallet"));
	rai::uint256_union inactive;
	if (!inactive.decode_hex (inactive_text))
	{
		rai::uint256_union active;
		if (!active.decode_hex (active_text))
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, true);
			std::shared_ptr <rai::wallet> inactive_wallet;
			std::shared_ptr <rai::wallet> active_wallet;
			if (!payment_wallets (transaction, inactive, active, inactive_wallet, active_wallet))
			{
				rai::account account (0);
				for (auto i (inactive_wallet->store.begin (transaction)), n (inactive_wallet->store.end ()); i != n && account.is_zero (); ++i)
				{
					rai::account first (i->first);
					if (rpc.node.ledger.account_balance (transaction, first).is_zero ())
					account = first;
				}
				if (account.is_zero ())
				{
					rai::keypair key;
					auto pub (active_wallet->store.insert (transaction, key.prv));
					assert (key.pub == pub);
					account = key.pub;
				}
				else
				{
					rai::raw_key key;
					auto error (inactive_wallet->store.fetch (transaction, account, key));
					assert (!error);
					inactive_wallet->store.erase (transaction, account);
					active_wallet->store.insert (transaction, key);
				}
				assert (!account.is_zero ());
				assert (!inactive_wallet->store.exists (transaction, account));
				assert (active_wallet->store.exists (transaction, account));
				boost::property_tree::ptree response_l;
				response_l.put ("account", account.to_base58check ());
				send_response (response_l);
			}
			else
			{
				error_response ("Unable to find wallets");
			}
		}
		else
		{
			error_response ("Bad active wallet number");
		}
	}
	else
	{
		error_response ("Bad inactive wallet number");
	}
}

void rai::rpc_handler::payment_check ()
{
	std::string inactive_text (request.get <std::string> ("inactive_wallet"));
	std::string active_text (request.get <std::string> ("active_wallet"));
	rai::uint256_union inactive;
	if (!inactive.decode_hex (inactive_text))
	{
		rai::uint256_union active;
		if (!active.decode_hex (active_text))
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, false);
			std::shared_ptr <rai::wallet> inactive_wallet;
			std::shared_ptr <rai::wallet> active_wallet;
			if (!payment_wallets (transaction, inactive, active, inactive_wallet, active_wallet))
			{
				if (inactive_wallet->store.valid_password (transaction))
				{
					if (active_wallet->store.valid_password (transaction))
					{
						boost::property_tree::ptree response_l;
						response_l.put ("status", "Ready");
						send_response (response_l);
					}
					else
					{
						boost::property_tree::ptree response_l;
						response_l.put ("status", "Active wallet locked");
						send_response (response_l);
					}
				}
				else
				{
					boost::property_tree::ptree response_l;
					response_l.put ("status", "Inactive wallet locked");
					send_response (response_l);
				}
			}
			else
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "Unable to find wallets");
				send_response (response_l);
			}
		}
		else
		{
			error_response ("Bad active wallet number");
		}
	}
	else
	{
		error_response ("Bad inactive wallet number");
	}
}

void rai::rpc_handler::payment_end ()
{
	std::string inactive_text (request.get <std::string> ("inactive_wallet"));
	std::string active_text (request.get <std::string> ("active_wallet"));
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union inactive;
	if (!inactive.decode_hex (inactive_text))
	{
		rai::uint256_union active;
		if (!active.decode_hex (active_text))
		{
			rai::transaction transaction (rpc.node.store.environment, nullptr, true);
			std::shared_ptr <rai::wallet> inactive_wallet;
			std::shared_ptr <rai::wallet> active_wallet;
			if (!payment_wallets (transaction, inactive, active, inactive_wallet, active_wallet))
			{
				rai::account account;
				if (!account.decode_base58check (account_text))
				{
					auto existing (active_wallet->store.find (transaction, account));
					if (existing != active_wallet->store.end ())
					{
						rai::raw_key key;
						auto error (active_wallet->store.fetch (transaction, account, key));
						active_wallet->store.erase (transaction, account);
						assert (!error);
						auto pub (inactive_wallet->store.insert (transaction, key));
						assert (pub == account);
						boost::property_tree::ptree response_l;
						send_response (response_l);
					}
					else
					{
						error_response ("Account not in active wallet");
					}
				}
				else
				{
					error_response ("Invalid account number");
				}
			}
			else
			{
				error_response ("Unable to find wallets");
			}
		}
		else
		{
			error_response ("Bad active wallet number");
		}
	}
	else
	{
		error_response ("Bad inactive wallet number");
	}
}

void rai::rpc_handler::payment_wait ()
{
	std::string account_text (request.get <std::string> ("account"));
	std::string amount_text (request.get <std::string> ("amount"));
	std::string timeout_text (request.get <std::string> ("timeout"));
	rai::uint256_union account;
	if (!account.decode_base58check (account_text))
	{
		rai::uint128_union amount;
		if (!amount.decode_dec (amount_text))
		{
			uint64_t timeout;
			if (!rpc.decode_unsigned (timeout_text, timeout))
			{
				std::chrono::system_clock::time_point cutoff (std::chrono::system_clock::now () + std::chrono::milliseconds (timeout));
				std::mutex mutex;
				std::condition_variable condition;
				rai::payment_observer observer (rpc, account, [&mutex, & condition] ()
				{
					std::lock_guard <std::mutex> lock (mutex);
					condition.notify_all ();
				});
				rai::payment_status status (rai::payment_status::unknown);
				{
					std::unique_lock <std::mutex> lock (mutex);
					while (rpc.node.balance (account) < amount.number () && std::chrono::system_clock::now () < cutoff)
					{
						condition.wait_until (lock, cutoff);
					}
				}
				if (rpc.node.balance (account) < amount.number ())
				{
					status = rai::payment_status::nothing;
				}
				else
				{
					status = rai::payment_status::success;
				}
				switch (status)
				{
					case rai::payment_status::nothing:
					{
						boost::property_tree::ptree response_l;
						response_l.put ("status", "nothing");
						send_response (response_l);
						break;
					}
					case rai::payment_status::success:
					{
						boost::property_tree::ptree response_l;
						response_l.put ("status", "success");
						send_response (response_l);
						break;
					}
					default:
					{
						error_response ("Internal payment error");
						break;
					}
				}
			}
			else
			{
				error_response ("Bad timeout number");
			}
		}
		else
		{
			error_response ("Bad amount number");
		}
	}
	else
	{
		error_response ("Bad account number");
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
		rpc.node.process_receive_republish (std::move (block), 0);
		boost::property_tree::ptree response_l;
		send_response (response_l);
	}
	else
	{
		error_response ("Block is invalid");
	}
}

void rai::rpc_handler::representative ()
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
			response_l.put ("representative", existing->second->store.representative (transaction).to_base58check ());
			send_response (response_l);
		}
		else
		{
			error_response ("Wallet not found");
		}
	}
	else
	{
		error_response ("Bad account number");
	}
}

void rai::rpc_handler::representative_set ()
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
				auto error (representative.decode_base58check (representative_text));
				if (!error)
				{
					rai::transaction transaction (rpc.node.store.environment, nullptr, true);
					existing->second->store.representative_set (transaction, representative);
					boost::property_tree::ptree response_l;
					response_l.put ("set", "1");
					send_response (response_l);
				}
				else
				{
					error_response ("Invalid account number");
				}
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad account number");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
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
				send_response (response_l);
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
	}
	else
	{
		error_response ("RPC control is disabled");
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
				auto error (source.decode_base58check (source_text));
				if (!error)
				{
					std::string destination_text (request.get <std::string> ("destination"));
					rai::account destination;
					auto error (destination.decode_base58check (destination_text));
					if (!error)
					{
						std::string amount_text (request.get <std::string> ("amount"));
						rai::amount amount;
						auto error (amount.decode_dec (amount_text));
						if (!error)
						{
							auto block (existing->second->send_async (source, destination, amount.number ()));
							boost::property_tree::ptree response_l;
							response_l.put ("block", block.to_string ());
							send_response (response_l);
						}
						else
						{
							error_response ("Bad amount format");
						}
					}
					else
					{
						error_response ("Bad destination account");
					}
				}
				else
				{
					error_response ("Bad source account");
				}
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad wallet number");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
	}
}

void rai::rpc_handler::version ()
{
	boost::property_tree::ptree response_l;
	response_l.put ("rpc_version", "1");
	response_l.put ("store_version", std::to_string (rpc.node.store_version ()));
	send_response (response_l);
}

void rai::rpc_handler::validate_account_number ()
{
	std::string account_text (request.get <std::string> ("account"));
	rai::uint256_union account;
	auto error (account.decode_base58check (account_text));
	boost::property_tree::ptree response_l;
	response_l.put ("valid", error ? "0" : "1");
	send_response (response_l);
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
					rai::transaction transaction (rpc.node.store.environment, nullptr, true);
					existing->second->store.insert (transaction, key);
					rai::public_key pub;
					ed25519_publickey (key.data.bytes.data (), pub.bytes.data ());
					boost::property_tree::ptree response_l;
					response_l.put ("account", pub.to_base58check ());
					send_response (response_l);
				}
				else
				{
					error_response ("Wallet not found");
				}
			}
			else
			{
				error_response ("Bad wallet number");
			}
		}
		else
		{
			error_response ("Bad private key");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
	}
}

void rai::rpc_handler::wallet_contains ()
{
	std::string account_text (request.get <std::string> ("account"));
	std::string wallet_text (request.get <std::string> ("wallet"));
	rai::uint256_union account;
	auto error (account.decode_base58check (account_text));
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
				send_response (response_l);
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad wallet number");
		}
	}
	else
	{
		error_response ("Bad account number");
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
		send_response (response_l);
	}
	else
	{
		error_response ("RPC control is disabled");
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
				send_response (response_l);
			}
			else
			{
				error_response ("Wallet not found");
			}
		}
		else
		{
			error_response ("Bad wallet number");
		}
	}
	else
	{
		error_response ("RPC control is disabled");
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
			send_response (response_l);
		}
		else
		{
			error_response ("Wallet not found");
		}
	}
	else
	{
		error_response ("Bad account number");
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
			send_response (response_l);
		}
		else
		{
			error_response ("Wallet not found");
		}
	}
	else
	{
		error_response ("Bad wallet number");
	}
}

void rai::rpc_handler::error_response (std::string const & message_a)
{
    auto response_l (boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request));
	auto response (std::make_shared <decltype (response_l)> (response_l));
	response->content = message_a;
	connection->write (response->to_buffers (), [response] (boost::system::error_code) {});
}

void rai::rpc::operator () (boost::network::http::async_server <rai::rpc>::request const & request_a, boost::network::http::async_server <rai::rpc>::connection_ptr connection_a)
{
	auto request_l (std::make_shared <boost::network::http::async_server <rai::rpc>::request> (request_a));
	auto existing (std::find_if (request_a.headers.begin (), request_a.headers.end (), [] (decltype(*request_a.headers.begin()) const & item_a)
	{
		return boost::to_lower_copy(item_a.name) == "content-length";
	}));
	if (existing != request_a.headers.end ())
	{
		uint64_t length;
		if (!decode_unsigned (existing->value, length))
		{
			if (length < 16384)
			{
				auto handler (std::make_shared <rai::rpc_handler> (*this, length, request_a, connection_a));
				handler->body.reserve (length);
				handler->read_or_process ();
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

void rai::rpc_handler::read_or_process ()
{
	if (body.size () < length)
	{
		auto this_l (shared_from_this ());
		connection->read ([this_l] (boost::network::http::async_server <rai::rpc>::connection::input_range range_a, boost::system::error_code error_a, size_t size_a, boost::network::http::async_server <rai::rpc>::connection_ptr connection_a)
		{
			this_l->part_handler (range_a, error_a, size_a);
		});
	}
	else
	{
		process_request ();
	}
}

void rai::rpc_handler::part_handler (boost::network::http::async_server <rai::rpc>::connection::input_range range_a, boost::system::error_code error_a, size_t size_a)
{
	if (!error_a)
	{
		body.append (range_a.begin (), range_a.begin () + size_a);
		read_or_process ();
	}
	else
	{
		BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Error in RPC request: %1%") % error_a.message ());
	}
}

void rai::rpc_handler::process_request ()
{
	if (headers.method == "POST")
	{
		try
		{
			std::stringstream istream (body);
			boost::property_tree::read_json (istream, request);
			std::string action (request.get <std::string> ("action"));
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
			else if (action == "account_weight")
			{
				account_weight ();
			}
			else if (action == "block")
			{
				block ();
			}
			else if (action == "chain")
			{
				chain ();
			}
			else if (action == "frontiers")
			{
				frontiers ();
			}
			else if (action == "keepalive")
			{
				keepalive ();
			}
			else if (action == "password_change")
			{
				password_change ();
			}
			else if (action == "password_enter")
			{
				password_enter ();
			}
			else if (action == "password_valid")
			{
				password_valid ();
			}
			else if (action == "payment_begin")
			{
				payment_begin ();
			}
			else if (action == "payment_check")
			{
				payment_check ();
			}
			else if (action == "payment_end")
			{
				payment_end ();
			}
            /*else if (action == "payment_shutdown")
			{
			}
            else if (action == "payment_startup")
			{
			}*/
			else if (action == "payment_wait")
			{
				payment_wait ();
			}
			else if (action == "price")
			{
				price ();
			}
			else if (action == "process")
			{
				process ();
			}
			else if (action == "representative")
			{
				representative ();
			}
			else if (action == "representative_set")
			{
				representative_set ();
			}
			else if (action == "search_pending")
			{
				search_pending ();
			}
			else if (action == "send")
			{
				send ();
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
			else
			{
				error_response ("Unknown command");
			}
		}
		catch (std::runtime_error const & err)
		{
			error_response ("Unable to parse JSON");
		}
		catch (...)
		{
			error_response ("Internal server error in RPC");
		}
	}
	else
	{
		error_response ("Can only POST requests");
	}
}

bool rai::rpc_handler::payment_wallets (MDB_txn * transaction_a, rai::uint256_union const & inactive_wallet_a, rai::uint256_union const & active_wallet_a, std::shared_ptr <rai::wallet> & inactive_a, std::shared_ptr <rai::wallet> & active_a)
{
	auto inactive_existing (rpc.node.wallets.items.find (inactive_wallet_a));
	auto active_existing (rpc.node.wallets.items.find (active_wallet_a));
	auto result (false);
	if (inactive_existing != rpc.node.wallets.items.end () && active_existing != rpc.node.wallets.items.end ())
	{
		inactive_a = inactive_existing->second;
		active_a = active_existing->second;
	}
	else
	{
		result = true;
	}
	return result;
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

rai::payment_observer::payment_observer (rai::rpc & rpc_a, rai::account const & account_a, std::function <void ()> const & action_a) :
rpc (rpc_a),
account (account_a)
{
	std::lock_guard <std::mutex> lock (rpc.mutex);
	assert (rpc.payment_observers.find (account_a) == rpc.payment_observers.end ());
	rpc.payment_observers [account_a] = action_a;
}

rai::payment_observer::~payment_observer ()
{
	std::lock_guard <std::mutex> lock (rpc.mutex);
	assert (rpc.payment_observers.find (account) != rpc.payment_observers.end ());
	rpc.payment_observers.erase (account);
}
