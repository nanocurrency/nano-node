#pragma once

#include <nano/lib/container_info.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/pending_info.hpp>

#include <deque>

namespace nano::bootstrap_ascending
{
struct account_database_scanner
{
	nano::ledger & ledger;

	std::deque<nano::account> next_batch (nano::store::transaction &, size_t batch_size);

	nano::account next{ 0 };
	size_t completed{ 0 };
};

struct pending_database_scanner
{
	nano::ledger & ledger;

	std::deque<nano::account> next_batch (nano::store::transaction &, size_t batch_size);

	nano::account next{ 0 };
	size_t completed{ 0 };
};

class database_scan
{
public:
	explicit database_scan (nano::ledger &);

	nano::account next (std::function<bool (nano::account const &)> const & filter);

	// Indicates if a full ledger iteration has taken place e.g. warmed up
	bool warmed_up () const;

	nano::container_info container_info () const;

private: // Dependencies
	nano::ledger & ledger;

private:
	void fill ();

private:
	account_database_scanner account_scanner;
	pending_database_scanner pending_scanner;

	std::deque<nano::account> queue;

	static size_t constexpr batch_size = 512;
};
}
