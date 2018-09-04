#include <rai/lib/errors.hpp>
#include <rai/lib/interface.h>
#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

void rai::rpc_handler::payment_begin ()
{
	std::string id_text (request.get<std::string> ("wallet"));
	rai::uint256_union id;
	if (!id.decode_hex (id_text))
	{
		auto existing (node.wallets.items.find (id));
		if (existing != node.wallets.items.end ())
		{
			auto transaction (node.store.tx_begin_write ());
			std::shared_ptr<rai::wallet> wallet (existing->second);
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
							BOOST_LOG (node.log) << boost::str (boost::format ("Transaction wallet %1% externally modified listing account %2% as free but no longer exists") % id.to_string () % account.to_account ());
							account.clear ();
						}
						else
						{
							if (!node.ledger.account_balance (transaction, account).is_zero ())
							{
								BOOST_LOG (node.log) << boost::str (boost::format ("Skipping account %1% for use as a transaction account: non-zero balance") % account.to_account ());
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
					response_l.put ("account", account.to_account ());
				}
				else
				{
					ec = nano::error_rpc::payment_unable_create_account;
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
	else
	{
		ec = nano::error_common::bad_wallet_number;
	}
	response_errors ();
}

void rai::rpc_handler::payment_init ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		if (wallet->store.valid_password (transaction))
		{
			wallet->init_free_accounts (transaction);
			response_l.put ("status", "Ready");
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	response_errors ();
}

void rai::rpc_handler::payment_end ()
{
	auto account (account_impl ());
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto existing (wallet->store.find (transaction, account));
		if (existing != wallet->store.end ())
		{
			if (node.ledger.account_balance (transaction, account).is_zero ())
			{
				wallet->free_accounts.insert (account);
				response_l.put ("ended", "1");
			}
			else
			{
				ec = nano::error_rpc::payment_account_balance;
			}
		}
		else
		{
			ec = nano::error_common::account_not_found_wallet;
		}
	}
	response_errors ();
}

void rai::rpc_handler::payment_wait ()
{
	std::string timeout_text (request.get<std::string> ("timeout"));
	auto account (account_impl ());
	auto amount (amount_impl ());
	if (!ec)
	{
		uint64_t timeout;
		if (!rai::decode_unsigned (timeout_text, timeout))
		{
			{
				auto observer (std::make_shared<rai::payment_observer> (response, rpc, account, amount));
				observer->start (timeout);
				std::lock_guard<std::mutex> lock (rpc.mutex);
				assert (rpc.payment_observers.find (account) == rpc.payment_observers.end ());
				rpc.payment_observers[account] = observer;
			}
			rpc.observer_action (account);
		}
		else
		{
			ec = nano::error_rpc::bad_timeout;
		}
	}
	if (ec)
	{
		response_errors ();
	}
}

void rai::rpc::observer_action (rai::account const & account_a)
{
	std::shared_ptr<rai::payment_observer> observer;
	{
		std::lock_guard<std::mutex> lock (mutex);
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
