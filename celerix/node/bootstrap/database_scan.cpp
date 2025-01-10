#include <celerix/lib/utility.hpp>
#include <celerix/node/bootstrap/crawlers.hpp>
#include <celerix/node/bootstrap/database_scan.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/pending.hpp>

/*
 * database_scan
 */

celerix::bootstrap::database_scan::database_scan (celerix::ledger & ledger_a) :
	ledger{ ledger_a },
	account_scanner{ ledger },
	pending_scanner{ ledger }
{
}

celerix::account celerix::bootstrap::database_scan::next (std::function<bool (celerix::account const &)> const & filter)
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

void celerix::bootstrap::database_scan::fill ()
{
	auto transaction = ledger.store.tx_begin_read ();

	auto set1 = account_scanner.next_batch (transaction, batch_size);
	auto set2 = pending_scanner.next_batch (transaction, batch_size);

	queue.insert (queue.end (), set1.begin (), set1.end ());
	queue.insert (queue.end (), set2.begin (), set2.end ());
}

bool celerix::bootstrap::database_scan::warmed_up () const
{
	return account_scanner.completed > 0 && pending_scanner.completed > 0;
}

celerix::container_info celerix::bootstrap::database_scan::container_info () const
{
	celerix::container_info info;
	info.put ("accounts_iterator", account_scanner.completed);
	info.put ("pending_iterator", pending_scanner.completed);
	return info;
}

/*
 * account_database_scanner
 */

std::deque<celerix::account> celerix::bootstrap::account_database_scanner::next_batch (celerix::store::transaction & transaction, size_t batch_size)
{
	std::deque<celerix::account> result;

	celerix::bootstrap::account_database_crawler crawler{ ledger.store, transaction, next };

	for (size_t count = 0; crawler.current && count < batch_size; crawler.advance (), ++count)
	{
		auto const & [account, info] = crawler.current.value ();
		result.push_back (account);
		next = inc_sat (account.number ());
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

std::deque<celerix::account> celerix::bootstrap::pending_database_scanner::next_batch (celerix::store::transaction & transaction, size_t batch_size)
{
	std::deque<celerix::account> result;

	celerix::bootstrap::pending_database_crawler crawler{ ledger.store, transaction, next };

	for (size_t count = 0; crawler.current && count < batch_size; crawler.advance (), ++count)
	{
		auto const & [key, info] = crawler.current.value ();
		result.push_back (key.account);
		next = inc_sat (key.account.number ());
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
