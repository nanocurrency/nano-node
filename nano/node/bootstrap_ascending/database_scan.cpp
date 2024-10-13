#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap_ascending/database_scan.hpp>
#include <nano/node/bootstrap_ascending/iterators.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/pending.hpp>

/*
 * database_scan
 */

nano::bootstrap_ascending::database_scan::database_scan (nano::ledger & ledger_a) :
	ledger{ ledger_a },
	account_scanner{ ledger },
	pending_scanner{ ledger }
{
}

nano::account nano::bootstrap_ascending::database_scan::next (std::function<bool (nano::account const &)> const & filter)
{
	if (queue.empty ())
	{
		fill ();
	}

	while (!queue.empty ())
	{
		auto result = queue.front ();
		queue.pop_front ();

		if (filter (result))
		{
			return result;
		}
	}

	return { 0 };
}

void nano::bootstrap_ascending::database_scan::fill ()
{
	auto transaction = ledger.store.tx_begin_read ();

	auto set1 = account_scanner.next_batch (transaction, batch_size);
	auto set2 = pending_scanner.next_batch (transaction, batch_size);

	queue.insert (queue.end (), set1.begin (), set1.end ());
	queue.insert (queue.end (), set2.begin (), set2.end ());
}

bool nano::bootstrap_ascending::database_scan::warmed_up () const
{
	return account_scanner.completed > 0 && pending_scanner.completed > 0;
}

nano::container_info nano::bootstrap_ascending::database_scan::container_info () const
{
	nano::container_info info;
	info.put ("accounts_iterator", account_scanner.completed);
	info.put ("pending_iterator", pending_scanner.completed);
	return info;
}

/*
 * account_database_scanner
 */

std::deque<nano::account> nano::bootstrap_ascending::account_database_scanner::next_batch (nano::store::transaction & transaction, size_t batch_size)
{
	std::deque<nano::account> result;

	account_database_crawler crawler{ ledger.store, transaction, next };

	for (size_t count = 0; crawler.current && count < batch_size; crawler.advance (), ++count)
	{
		auto const & [account, info] = crawler.current.value ();
		result.push_back (account);
		next = account.number () + 1; // TODO: Handle account number overflow
	}

	// Empty current value indicates the end of the table
	if (!crawler.current)
	{
		// Reset for the next ledger iteration
		next = { 0 };
		++completed;
	}

	return result;
}

/*
 * pending_database_scanner
 */

std::deque<nano::account> nano::bootstrap_ascending::pending_database_scanner::next_batch (nano::store::transaction & transaction, size_t batch_size)
{
	std::deque<nano::account> result;

	pending_database_crawler crawler{ ledger.store, transaction, next };

	for (size_t count = 0; crawler.current && count < batch_size; crawler.advance (), ++count)
	{
		auto const & [key, info] = crawler.current.value ();
		result.push_back (key.account);
		next = key.account.number () + 1; // TODO: Handle account number overflow
	}

	// Empty current value indicates the end of the table
	if (!crawler.current)
	{
		// Reset for the next ledger iteration
		next = { 0 };
		++completed;
	}

	return result;
}
