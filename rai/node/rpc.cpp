#include <rai/node/rpc.hpp>

#include <rai/node/node.hpp>

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
server (decltype (server)::options (*this).address (config.address.to_string ()).port (std::to_string (config.port)).io_service (service_a).thread_pool (pool_a)),
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

namespace
{
void set_response (boost::network::http::server <rai::rpc>::response & response, boost::property_tree::ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    response.status = boost::network::http::server <rai::rpc>::response::ok;
    response.headers.push_back (boost::network::http::response_header_narrow {"Content-Type", "application/json"});
    response.content = ostream.str ();
}
}

void rai::rpc::operator () (boost::network::http::server <rai::rpc>::request const & request, boost::network::http::server <rai::rpc>::response & response)
{
    if (request.method == "POST")
    {
        try
        {
            boost::property_tree::ptree request_l;
            std::stringstream istream (request.body);
            boost::property_tree::read_json (istream, request_l);
            std::string action (request_l.get <std::string> ("action"));
            if (node.config.logging.log_rpc ())
            {
                BOOST_LOG (node.log) << request.body;
            }
            if (action == "account_balance")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto balance (node.balance (account));
                    boost::property_tree::ptree response_l;
                    response_l.put ("balance", balance.convert_to <std::string> ());
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "account_create")
            {
                if (config.enable_control)
                {
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (node.wallets.items.find (wallet));
                        if (existing != node.wallets.items.end ())
                        {
                            rai::keypair new_key;
                            existing->second->insert (new_key.prv);
                            boost::property_tree::ptree response_l;
                            response_l.put ("account", new_key.pub.to_base58check ());
                            set_response (response, response_l);
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "account_list")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (node.wallets.items.find (wallet));
                    if (existing != node.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        boost::property_tree::ptree accounts;
						rai::transaction transaction (node.store.environment, nullptr, false);
                        for (auto i (existing->second->store.begin (transaction)), j (existing->second->store.end ()); i != j; ++i)
                        {
                            boost::property_tree::ptree entry;
                            entry.put ("", rai::uint256_union (i->first).to_base58check ());
                            accounts.push_back (std::make_pair ("", entry));
                        }
                        response_l.add_child ("accounts", accounts);
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad wallet number";
                }
            }
            else if (action == "account_move")
            {
				if (config.enable_control)
				{
					std::string wallet_text (request_l.get <std::string> ("wallet"));
					std::string source_text (request_l.get <std::string> ("source"));
					auto accounts_text (request_l.get_child ("accounts"));
					rai::uint256_union wallet;
					auto error (wallet.decode_hex (wallet_text));
					if (!error)
					{
						auto existing (node.wallets.items.find (wallet));
						if (existing != node.wallets.items.end ())
						{
							auto wallet (existing->second);
							rai::uint256_union source;
							auto error (source.decode_hex (source_text));
							if (!error)
							{
								auto existing (node.wallets.items.find (source));
								if (existing != node.wallets.items.end ())
								{
									auto source (existing->second);
									std::vector <rai::public_key> accounts;
									for (auto i (accounts_text.begin ()), n (accounts_text.end ()); i != n; ++i)
									{
										rai::public_key account;
										account.decode_hex (i->second.get <std::string> (""));
										accounts.push_back (account);
									}
									rai::transaction transaction (node.store.environment, nullptr, true);
									auto error (wallet->store.move (transaction, source->store, accounts));
									boost::property_tree::ptree response_l;
									response_l.put ("moved", error ? "0" : "1");
									set_response (response, response_l);
								}
								else
								{
									response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
									response.content = "Source not found";
								}
							}
							else
							{
								response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
								response.content = "Bad source number";
							}
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Wallet not found";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad wallet number";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "account_weight")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto balance (node.weight (account));
                    boost::property_tree::ptree response_l;
                    response_l.put ("weight", balance.convert_to <std::string> ());
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "block")
            {
                std::string hash_text (request_l.get <std::string> ("hash"));
				rai::uint256_union hash;
                auto error (hash.decode_hex (hash_text));
                if (!error)
                {
					rai::transaction transaction (node.store.environment, nullptr, false);
					auto block (node.store.block_get (transaction, hash));
					if (block != nullptr)
                    {
                        boost::property_tree::ptree response_l;
						std::string contents;
						block->serialize_json (contents);
						response_l.put ("contents", contents);
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Block not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad hash number";
                }
            }
			else if (action == "chain")
			{
				std::string block_text (request_l.get <std::string> ("block"));
				std::string count_text (request_l.get <std::string> ("count"));
				rai::block_hash block;
				if (!block.decode_hex (block_text))
				{
					uint64_t count;
					if (!decode_unsigned (count_text, count))
					{
						boost::property_tree::ptree response_l;
						boost::property_tree::ptree blocks;
						rai::transaction transaction (node.store.environment, nullptr, false);
						while (!block.is_zero () && blocks.size () < count)
						{
							auto block_l (node.store.block_get (transaction, block));
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
						set_response (response, response_l);
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Invalid count limit";
					}
				}
				else
				{
					response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
					response.content = "Invalid block hash";
				}
			}
            else if (action == "frontiers")
            {
				std::string account_text (request_l.get <std::string> ("account"));
				std::string count_text (request_l.get <std::string> ("count"));
				rai::account start;
				if (!start.decode_base58check (account_text))
				{
					uint64_t count;
					if (!decode_unsigned (count_text, count))
					{
						boost::property_tree::ptree response_l;
						boost::property_tree::ptree frontiers;
						rai::transaction transaction (node.store.environment, nullptr, false);
						for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && frontiers.size () < count; ++i)
						{
							frontiers.put (rai::account (i->first).to_base58check (), rai::account_info (i->second).head.to_string ());
						}
						response_l.add_child ("frontiers", frontiers);
						set_response (response, response_l);
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Invalid count limit";
					}
				}
				else
				{
					response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
					response.content = "Invalid starting account";
				}
            }
            else if (action == "keepalive")
            {
				if (config.enable_control)
				{
					std::string address_text (request_l.get <std::string> ("address"));
					std::string port_text (request_l.get <std::string> ("port"));
					uint16_t port;
					if (!rai::parse_port (port_text, port))
					{
						node.keepalive (address_text, port);
						boost::property_tree::ptree response_l;
						set_response (response, response_l);
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Invalid port";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "password_change")
            {
				if (config.enable_control)
				{
					std::string wallet_text (request_l.get <std::string> ("wallet"));
					rai::uint256_union wallet;
					auto error (wallet.decode_hex (wallet_text));
					if (!error)
					{
						auto existing (node.wallets.items.find (wallet));
						if (existing != node.wallets.items.end ())
						{
							rai::transaction transaction (node.store.environment, nullptr, true);
							boost::property_tree::ptree response_l;
							std::string password_text (request_l.get <std::string> ("password"));
							auto error (existing->second->store.rekey (transaction, password_text));
							response_l.put ("changed", error ? "0" : "1");
							set_response (response, response_l);
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Wallet not found";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad account number";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "password_enter")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (node.wallets.items.find (wallet));
                    if (existing != node.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        std::string password_text (request_l.get <std::string> ("password"));
                        auto error (existing->second->enter_password (password_text));
                        response_l.put ("valid", error ? "0" : "1");
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "password_valid")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (node.wallets.items.find (wallet));
                    if (existing != node.wallets.items.end ())
                    {
						rai::transaction transaction (node.store.environment, nullptr, false);
                        boost::property_tree::ptree response_l;
                        response_l.put ("valid", existing->second->store.valid_password (transaction) ? "1" : "0");
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "payment_begin")
			{
                std::string inactive_text (request_l.get <std::string> ("inactive_wallet"));
                std::string active_text (request_l.get <std::string> ("active_wallet"));
				rai::uint256_union inactive;
				if (!inactive.decode_hex (inactive_text))
				{
					rai::uint256_union active;
					if (!active.decode_hex (active_text))
					{
						rai::transaction transaction (node.store.environment, nullptr, true);
						std::shared_ptr <rai::wallet> inactive_wallet;
						std::shared_ptr <rai::wallet> active_wallet;
						if (!payment_wallets (transaction, inactive, active, inactive_wallet, active_wallet))
						{
							rai::account account (0);
							for (auto i (inactive_wallet->store.begin (transaction)), n (inactive_wallet->store.end ()); i != n && account.is_zero (); ++i)
							{
								rai::account first (i->first);
								if (node.ledger.account_balance (transaction, first).is_zero ())
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
							set_response (response, response_l);
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Unable to find wallets";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad active wallet number";
					}
				}
				else
				{
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad inactive wallet number";
				}
			}
            else if (action == "payment_check")
			{
                std::string inactive_text (request_l.get <std::string> ("inactive_wallet"));
                std::string active_text (request_l.get <std::string> ("active_wallet"));
				rai::uint256_union inactive;
				if (!inactive.decode_hex (inactive_text))
				{
					rai::uint256_union active;
					if (!active.decode_hex (active_text))
					{
						rai::transaction transaction (node.store.environment, nullptr, false);
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
									set_response (response, response_l);
								}
								else
								{
									boost::property_tree::ptree response_l;
									response_l.put ("status", "Active wallet locked");
									set_response (response, response_l);
								}
							}
							else
							{
								boost::property_tree::ptree response_l;
								response_l.put ("status", "Inactive wallet locked");
								set_response (response, response_l);
							}
						}
						else
						{
							boost::property_tree::ptree response_l;
							response_l.put ("status", "Unable to find wallets");
							set_response (response, response_l);
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad active wallet number";
					}
				}
				else
				{
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad inactive wallet number";
				}
			}
            else if (action == "payment_end")
			{
                std::string inactive_text (request_l.get <std::string> ("inactive_wallet"));
                std::string active_text (request_l.get <std::string> ("active_wallet"));
				std::string account_text (request_l.get <std::string> ("account"));
				rai::uint256_union inactive;
				if (!inactive.decode_hex (inactive_text))
				{
					rai::uint256_union active;
					if (!active.decode_hex (active_text))
					{
						rai::transaction transaction (node.store.environment, nullptr, true);
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
									set_response (response, response_l);
								}
								else
								{
									response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
									response.content = "Account not in active wallet";
								}
							}
							else
							{
								response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
								response.content = "Invalid account number";
							}
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Unable to find wallets";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad active wallet number";
					}
				}
				else
				{
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad inactive wallet number";
				}
			}
            /*else if (action == "payment_shutdown")
			{
			}
            else if (action == "payment_startup")
			{
			}*/
            else if (action == "payment_wait")
            {
                std::string account_text (request_l.get <std::string> ("account"));
				std::string amount_text (request_l.get <std::string> ("amount"));
				std::string timeout_text (request_l.get <std::string> ("timeout"));
                rai::uint256_union account;
                if (!account.decode_base58check (account_text))
                {
					rai::uint128_union amount;
					if (!amount.decode_dec (amount_text))
					{
						uint64_t timeout;
						if (!decode_unsigned (timeout_text, timeout))
						{
							std::chrono::system_clock::time_point cutoff (std::chrono::system_clock::now () + std::chrono::milliseconds (timeout));
							std::mutex mutex;
							std::condition_variable condition;
							rai::payment_observer observer (*this, account, [&mutex, & condition] ()
							{
								std::lock_guard <std::mutex> lock (mutex);
								condition.notify_all ();
							});
							rai::payment_status status (rai::payment_status::unknown);
							{
								std::unique_lock <std::mutex> lock (mutex);
								while (node.balance (account) < amount.number () && std::chrono::system_clock::now () < cutoff)
								{
									condition.wait_until (lock, cutoff);
								}
								
							}
							if (node.balance (account) < amount.number ())
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
									set_response (response, response_l);
									break;
								}
								case rai::payment_status::success:
								{
									boost::property_tree::ptree response_l;
									response_l.put ("status", "success");
									set_response (response, response_l);
									break;
								}
								default:
								{
									response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::internal_server_error);
									response.content = "Internal payment error";
									break;
								}
							}
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Bad timeout number";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad amount number";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "price")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
					auto amount_text (request_l.get <std::string> ("amount"));
					uint64_t amount;
					if (!decode_unsigned (amount_text, amount))
					{
						if (amount <= 1000)
						{
							auto balance (node.balance (account));
							if (balance >= amount * rai::Grai_ratio)
							{
								auto price (node.price (balance, amount));
								boost::property_tree::ptree response_l;
								response_l.put ("price", std::to_string (price));
								set_response (response, response_l);
							}
							else
							{
								response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
								response.content = "Requesting more blocks than are available";
							}
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Cannot purchase more than 1000";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Invalid amount";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "process")
            {
                std::string block_text (request_l.get <std::string> ("block"));
				boost::property_tree::ptree block_l;
				std::stringstream block_stream (block_text);
				boost::property_tree::read_json (block_stream, block_l);
				auto block (rai::deserialize_block_json (block_l));
				if (block != nullptr)
				{
					node.process_receive_republish (std::move (block), 0);
                    boost::property_tree::ptree response_l;
					set_response (response, response_l);
				}
				else
				{
					response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
					response.content = "Block is invalid";
				}
            }
            else if (action == "representative")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (node.wallets.items.find (wallet));
                    if (existing != node.wallets.items.end ())
                    {
						rai::transaction transaction (node.store.environment, nullptr, false);
                        boost::property_tree::ptree response_l;
                        response_l.put ("representative", existing->second->store.representative (transaction).to_base58check ());
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "representative_set")
            {
				if (config.enable_control)
				{
					std::string wallet_text (request_l.get <std::string> ("wallet"));
					rai::uint256_union wallet;
					auto error (wallet.decode_hex (wallet_text));
					if (!error)
					{
						auto existing (node.wallets.items.find (wallet));
						if (existing != node.wallets.items.end ())
						{
							std::string representative_text (request_l.get <std::string> ("representative"));
							rai::account representative;
							auto error (representative.decode_base58check (representative_text));
							if (!error)
							{
								rai::transaction transaction (node.store.environment, nullptr, true);
								existing->second->store.representative_set (transaction, representative);
								boost::property_tree::ptree response_l;
								response_l.put ("set", "1");
								set_response (response, response_l);
							}
							else
							{
								response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
								response.content = "Invalid account number";
							}
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Wallet not found";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad account number";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "search_pending")
            {
				if (config.enable_control)
				{
					std::string wallet_text (request_l.get <std::string> ("wallet"));
					rai::uint256_union wallet;
					auto error (wallet.decode_hex (wallet_text));
					if (!error)
					{
						auto existing (node.wallets.items.find (wallet));
						if (existing != node.wallets.items.end ())
						{
							auto error (existing->second->search_pending ());
							boost::property_tree::ptree response_l;
							response_l.put ("started", !error);
							set_response (response, response_l);
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Wallet not found";
						}
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "send")
            {
                if (config.enable_control)
                {
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (node.wallets.items.find (wallet));
                        if (existing != node.wallets.items.end ())
                        {
							std::string source_text (request_l.get <std::string> ("source"));
							rai::account source;
							auto error (source.decode_base58check (source_text));
							if (!error)
							{
								std::string destination_text (request_l.get <std::string> ("destination"));
								rai::account destination;
								auto error (destination.decode_base58check (destination_text));
								if (!error)
								{
									std::string amount_text (request_l.get <std::string> ("amount"));
									rai::amount amount;
									auto error (amount.decode_dec (amount_text));
									if (!error)
									{
										auto block (existing->second->send_sync (source, destination, amount.number ()));
										boost::property_tree::ptree response_l;
										response_l.put ("block", block.to_string ());
										set_response (response, response_l);
									}
									else
									{
										response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
										response.content = "Bad amount format";
									}
								}
								else
								{
									response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
									response.content = "Bad destination account";
								}
							}
							else
							{
								response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
								response.content = "Bad source account";
							}
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "validate_account_number")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                boost::property_tree::ptree response_l;
                response_l.put ("valid", error ? "0" : "1");
                set_response (response, response_l);
            }
            else if (action == "wallet_add")
            {
                if (config.enable_control)
                {
                    std::string key_text (request_l.get <std::string> ("key"));
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::raw_key key;
                    auto error (key.data.decode_hex (key_text));
                    if (!error)
                    {
                        rai::uint256_union wallet;
                        auto error (wallet.decode_hex (wallet_text));
                        if (!error)
                        {
                            auto existing (node.wallets.items.find (wallet));
                            if (existing != node.wallets.items.end ())
                            {
								rai::transaction transaction (node.store.environment, nullptr, true);
                                existing->second->store.insert (transaction, key);
                                rai::public_key pub;
                                ed25519_publickey (key.data.bytes.data (), pub.bytes.data ());
                                boost::property_tree::ptree response_l;
                                response_l.put ("account", pub.to_base58check ());
                                set_response (response, response_l);
                            }
                            else
                            {
                                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                response.content = "Wallet not found";
                            }
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Bad wallet number";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad private key";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "wallet_contains")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (node.wallets.items.find (wallet));
                        if (existing != node.wallets.items.end ())
                        {
							rai::transaction transaction (node.store.environment, nullptr, false);
                            auto exists (existing->second->store.find (transaction, account) != existing->second->store.end ());
                            boost::property_tree::ptree response_l;
                            response_l.put ("exists", exists ? "1" : "0");
                            set_response (response, response_l);
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_create")
            {
				if (config.enable_control)
				{
					rai::keypair wallet_id;
					auto wallet (node.wallets.create (wallet_id.pub));
					boost::property_tree::ptree response_l;
					response_l.put ("wallet", wallet_id.pub.to_string ());
					set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "wallet_destroy")
            {
				if (config.enable_control)
				{
					std::string wallet_text (request_l.get <std::string> ("wallet"));
					rai::uint256_union wallet;
					auto error (wallet.decode_hex (wallet_text));
					if (!error)
					{
						auto existing (node.wallets.items.find (wallet));
						if (existing != node.wallets.items.end ())
						{
							node.wallets.destroy (wallet);
							boost::property_tree::ptree response_l;
							set_response (response, response_l);
						}
						else
						{
							response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
							response.content = "Wallet not found";
						}
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Bad wallet number";
					}
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "wallet_export")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (node.wallets.items.find (wallet));
                    if (existing != node.wallets.items.end ())
                    {
						rai::transaction transaction (node.store.environment, nullptr, false);
                        std::string json;
                        existing->second->store.serialize_json (transaction, json);
                        boost::property_tree::ptree response_l;
                        response_l.put ("json", json);
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_key_valid")
            {
				std::string wallet_text (request_l.get <std::string> ("wallet"));
				rai::uint256_union wallet;
				auto error (wallet.decode_hex (wallet_text));
				if (!error)
				{
					auto existing (node.wallets.items.find (wallet));
					if (existing != node.wallets.items.end ())
					{
						rai::transaction transaction (node.store.environment, nullptr, false);
						auto valid (existing->second->store.valid_password (transaction));
						boost::property_tree::ptree response_l;
						response_l.put ("valid", valid ? "1" : "0");
						set_response (response, response_l);
					}
					else
					{
						response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
						response.content = "Wallet not found";
					}
				}
				else
				{
					response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
					response.content = "Bad wallet number";
				}
			}
            else
            {
                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                response.content = "Unknown command";
            }
        }
        catch (std::runtime_error const &)
        {
            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
            response.content = "Unable to parse JSON";
        }
		catch (...)
        {
            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
            response.content = "Internal server error in RPC";
        }
    }
    else
    {
        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::method_not_allowed);
        response.content = "Can only POST requests";
    }
}

bool rai::rpc::payment_wallets (MDB_txn * transaction_a, rai::uint256_union const & inactive_wallet_a, rai::uint256_union const & active_wallet_a, std::shared_ptr <rai::wallet> & inactive_a, std::shared_ptr <rai::wallet> & active_a)
{
	auto inactive_existing (node.wallets.items.find (inactive_wallet_a));
	auto active_existing (node.wallets.items.find (active_wallet_a));
	auto result (false);
	if (inactive_existing != node.wallets.items.end () && active_existing != node.wallets.items.end ())
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