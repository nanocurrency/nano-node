#include <queue>
#include <rai/node/common.hpp>
#include <rai/node/wallet.hpp>
#include <rai/secure/blockstore.hpp>

#include <boost/polymorphic_cast.hpp>

rai::amount_visitor::amount_visitor (rai::transaction const & transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_amount (0),
current_balance (0),
amount (0)
{
}

void rai::amount_visitor::send_block (rai::send_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void rai::amount_visitor::receive_block (rai::receive_block const & block_a)
{
	current_amount = block_a.hashables.source;
}

void rai::amount_visitor::open_block (rai::open_block const & block_a)
{
	if (block_a.hashables.source != rai::genesis_account)
	{
		current_amount = block_a.hashables.source;
	}
	else
	{
		amount = rai::genesis_amount;
		current_amount = 0;
	}
}

void rai::amount_visitor::state_block (rai::state_block const & block_a)
{
	current_balance = block_a.hashables.previous;
	amount = block_a.hashables.balance.number ();
	current_amount = 0;
}

void rai::amount_visitor::change_block (rai::change_block const & block_a)
{
	amount = 0;
	current_amount = 0;
}

void rai::amount_visitor::compute (rai::block_hash const & block_hash)
{
	current_amount = block_hash;
	while (!current_amount.is_zero () || !current_balance.is_zero ())
	{
		if (!current_amount.is_zero ())
		{
			auto block (store.block_get (transaction, current_amount));
			if (block != nullptr)
			{
				block->visit (*this);
			}
			else
			{
				if (block_hash == rai::genesis_account)
				{
					amount = std::numeric_limits<rai::uint128_t>::max ();
					current_amount = 0;
				}
				else
				{
					assert (false);
					amount = 0;
					current_amount = 0;
				}
			}
		}
		else
		{
			balance_visitor prev (transaction, store);
			prev.compute (current_balance);
			amount = amount < prev.balance ? prev.balance - amount : amount - prev.balance;
			current_balance = 0;
		}
	}
}

rai::balance_visitor::balance_visitor (rai::transaction const & transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
current_balance (0),
current_amount (0),
balance (0)
{
}

void rai::balance_visitor::send_block (rai::send_block const & block_a)
{
	balance += block_a.hashables.balance.number ();
	current_balance = 0;
}

void rai::balance_visitor::receive_block (rai::receive_block const & block_a)
{
	rai::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		balance += block_info.balance.number ();
		current_balance = 0;
	}
	else
	{
		current_amount = block_a.hashables.source;
		current_balance = block_a.hashables.previous;
	}
}

void rai::balance_visitor::open_block (rai::open_block const & block_a)
{
	current_amount = block_a.hashables.source;
	current_balance = 0;
}

void rai::balance_visitor::change_block (rai::change_block const & block_a)
{
	rai::block_info block_info;
	if (!store.block_info_get (transaction, block_a.hash (), block_info))
	{
		balance += block_info.balance.number ();
		current_balance = 0;
	}
	else
	{
		current_balance = block_a.hashables.previous;
	}
}

void rai::balance_visitor::state_block (rai::state_block const & block_a)
{
	balance = block_a.hashables.balance.number ();
	current_balance = 0;
}

void rai::balance_visitor::compute (rai::block_hash const & block_hash)
{
	current_balance = block_hash;
	while (!current_balance.is_zero () || !current_amount.is_zero ())
	{
		if (!current_amount.is_zero ())
		{
			amount_visitor source (transaction, store);
			source.compute (current_amount);
			balance += source.amount;
			current_amount = 0;
		}
		else
		{
			auto block (store.block_get (transaction, current_balance));
			assert (block != nullptr);
			block->visit (*this);
		}
	}
}

rai::representative_visitor::representative_visitor (rai::transaction const & transaction_a, rai::block_store & store_a) :
transaction (transaction_a),
store (store_a),
result (0)
{
}

void rai::representative_visitor::compute (rai::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		assert (block != nullptr);
		block->visit (*this);
	}
}

void rai::representative_visitor::send_block (rai::send_block const & block_a)
{
	current = block_a.previous ();
}

void rai::representative_visitor::receive_block (rai::receive_block const & block_a)
{
	current = block_a.previous ();
}

void rai::representative_visitor::open_block (rai::open_block const & block_a)
{
	result = block_a.hash ();
}

void rai::representative_visitor::change_block (rai::change_block const & block_a)
{
	result = block_a.hash ();
}

void rai::representative_visitor::state_block (rai::state_block const & block_a)
{
	result = block_a.hash ();
}
