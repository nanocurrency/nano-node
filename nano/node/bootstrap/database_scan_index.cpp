#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap/database_scan_index.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/store/ledger/pending.hpp>

namespace nano::bootstrap
{
/*
 * database_scan_index
 */

database_scan_index::database_scan_index (nano::ledger & ledger_a) :
	ledger{ ledger_a },
	account_scanner{ ledger },
	pending_scanner{ ledger }
{
}

void database_scan_index::reset ()
{
	queue.clear ();

	account_scanner.next = nano::account{ 0 };
	account_scanner.completed = 0;

	pending_scanner.next = nano::account{ 0 };
	pending_scanner.completed = 0;
}

std::optional<blocks_query> database_scan_index::next (std::function<bool (nano::account const &)> const & filter)
{
	if (queue.empty ())
	{
		fill ();
	}

	while (!queue.empty ())
	{
		auto result = queue.front ();
		queue.pop_front ();

		if (filter (result.account))
		{
			return result;
		}
	}

	return std::nullopt;
}

void database_scan_index::fill ()
{
	auto transaction = ledger.store.tx_begin_read ();

	auto set1 = account_scanner.next_batch (transaction, batch_size);
	auto set2 = pending_scanner.next_batch (transaction, batch_size);

	// Build the pull queries while the scan transaction is open, so no secondary lookup is needed on the request path
	for (auto const & account : set1)
	{
		queue.push_back (prepare_query (transaction, account));
	}
	for (auto const & account : set2)
	{
		queue.push_back (prepare_query (transaction, account));
	}
}

blocks_query database_scan_index::prepare_query (nano::store::transaction & transaction, nano::account const & account) const
{
	// The database scan always issues safe requests, starting from the confirmed frontier when available.
	// Accounts without a confirmation height (new or pending-only accounts) are pulled from the account root.
	blocks_query query{};
	query.account = account;
	query.count = pull_count;
	query.type = query_type::blocks_by_account;
	query.start = account;

	if (auto conf_info = ledger.store.confirmation_height.get (transaction, account))
	{
		query.type = query_type::blocks_by_hash;
		query.start = conf_info->frontier;
	}

	return query;
}

bool database_scan_index::warmed_up () const
{
	return account_scanner.completed > 0 && pending_scanner.completed > 0;
}

nano::container_info database_scan_index::container_info () const
{
	nano::container_info info;
	info.put ("accounts_iterator", account_scanner.completed);
	info.put ("pending_iterator", pending_scanner.completed);
	return info;
}

/*
 * account_database_scanner
 */

std::deque<nano::account> account_database_scanner::next_batch (nano::store::transaction & transaction, size_t batch_size)
{
	std::deque<nano::account> result;

	auto crawler = ledger.store.account.crawl (transaction, next);

	for (; crawler && result.size () < batch_size; ++crawler)
	{
		auto const & [account, info] = *crawler;
		result.push_back (account);
	}

	// Empty crawler indicates end of ledger
	if (!crawler)
	{
		// Reset for the next ledger iteration
		next = { 0 };
		++completed;
	}
	else
	{
		next = crawler.group_key ();
	}

	return result;
}

/*
 * pending_database_scanner
 */

std::deque<nano::account> pending_database_scanner::next_batch (nano::store::transaction & transaction, size_t batch_size)
{
	std::deque<nano::account> result;

	auto crawler = ledger.store.pending.crawl (transaction, next);

	// Advance group-wise to visit each account once, regardless of its number of pending entries
	for (; crawler && result.size () < batch_size; crawler.next_group ())
	{
		auto const & [key, info] = *crawler;
		result.push_back (key.account);
	}

	// Empty crawler indicates end of ledger
	if (!crawler)
	{
		// Reset for the next ledger iteration
		next = { 0 };
		++completed;
	}
	else
	{
		next = crawler.group_key ();
	}

	return result;
}
}
