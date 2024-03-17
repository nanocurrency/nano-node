#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap_ascending/iterators.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/pending.hpp>

/*
 * database_iterator
 */

nano::bootstrap_ascending::database_iterator::database_iterator (nano::ledger & ledger, table_type table_a) :
	ledger{ ledger },
	table{ table_a }
{
}

nano::account nano::bootstrap_ascending::database_iterator::operator* () const
{
	return current;
}

void nano::bootstrap_ascending::database_iterator::next (secure::transaction & tx)
{
	switch (table)
	{
		case table_type::account:
		{
			auto i = current.number () + 1;
			auto item = ledger.store.account.begin (tx, i);
			if (item != ledger.store.account.end ())
			{
				current = item->first;
			}
			else
			{
				current = { 0 };
			}
			break;
		}
		case table_type::pending:
		{
			auto item = ledger.any.receivable_upper_bound (tx, current);
			if (item != ledger.any.receivable_end ())
			{
				current = item->first.account;
			}
			else
			{
				current = { 0 };
			}
			break;
		}
	}
}

/*
 * buffered_iterator
 */

nano::bootstrap_ascending::buffered_iterator::buffered_iterator (nano::ledger & ledger) :
	ledger{ ledger },
	accounts_iterator{ ledger, database_iterator::table_type::account },
	pending_iterator{ ledger, database_iterator::table_type::pending }
{
}

nano::account nano::bootstrap_ascending::buffered_iterator::operator* () const
{
	return !buffer.empty () ? buffer.front () : nano::account{ 0 };
}

nano::account nano::bootstrap_ascending::buffered_iterator::next ()
{
	if (!buffer.empty ())
	{
		buffer.pop_front ();
	}
	else
	{
		fill ();
	}

	return *(*this);
}

bool nano::bootstrap_ascending::buffered_iterator::warmup () const
{
	return warmup_m;
}

void nano::bootstrap_ascending::buffered_iterator::fill ()
{
	debug_assert (buffer.empty ());

	// Fill half from accounts table and half from pending table
	auto transaction = ledger.tx_begin_read ();

	for (int n = 0; n < size / 2; ++n)
	{
		accounts_iterator.next (transaction);
		if (!(*accounts_iterator).is_zero ())
		{
			buffer.push_back (*accounts_iterator);
		}
	}

	for (int n = 0; n < size / 2; ++n)
	{
		pending_iterator.next (transaction);
		if (!(*pending_iterator).is_zero ())
		{
			buffer.push_back (*pending_iterator);
		}
		else
		{
			warmup_m = false;
		}
	}
}
