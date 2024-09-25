#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/pending_info.hpp>

#include <deque>

namespace nano::bootstrap_ascending
{
struct account_database_iterator
{
	explicit account_database_iterator (nano::ledger &);

	std::deque<nano::account> next_batch (nano::store::transaction &, size_t batch_size);
	bool warmed_up () const;

	nano::ledger & ledger;
	nano::account next{ 0 };
	size_t completed{ 0 };
};

struct pending_database_iterator
{
	explicit pending_database_iterator (nano::ledger &);

	std::deque<nano::account> next_batch (nano::store::transaction &, size_t batch_size);
	bool warmed_up () const;

	nano::ledger & ledger;
	nano::pending_key next{ 0, 0 };
	size_t completed{ 0 };
};

class database_scan
{
public:
	explicit database_scan (nano::ledger &);

	nano::account next (std::function<bool (nano::account const &)> const & filter);

	// Indicates if a full ledger iteration has taken place e.g. warmed up
	bool warmed_up () const;

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name) const;

private: // Dependencies
	nano::ledger & ledger;

private:
	void fill ();

private:
	account_database_iterator accounts_iterator;
	pending_database_iterator pending_iterator;

	std::deque<nano::account> queue;

	static size_t constexpr batch_size = 512;
};
}
