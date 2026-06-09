#pragma once

#include <nano/lib/container_info.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/pending_info.hpp>

#include <deque>
#include <optional>

namespace nano::bootstrap
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

class database_scan_index
{
public:
	explicit database_scan_index (nano::ledger &);

	// Returns the next safe pull query for an account that passes the filter, or nullopt when none are currently available
	std::optional<blocks_query> next (std::function<bool (nano::account const &)> const & filter);

	// Indicates if a full ledger iteration has taken place e.g. warmed up
	bool warmed_up () const;

	void reset ();

	nano::container_info container_info () const;

private: // Dependencies
	nano::ledger & ledger;

private:
	void fill ();

	// Builds a safe pull query for the given account, starting from the confirmed frontier when available
	blocks_query prepare_query (nano::store::transaction &, nano::account const &) const;

private:
	account_database_scanner account_scanner;
	pending_database_scanner pending_scanner;

	std::deque<blocks_query> queue;

	static size_t constexpr batch_size = 512;
	static size_t constexpr pull_count = 2;
};
}
