#pragma once

#include <celerix/lib/container_info.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/pending_info.hpp>

#include <deque>

namespace celerix::bootstrap
{
struct account_database_scanner
{
	celerix::ledger & ledger;

	std::deque<celerix::account> next_batch (celerix::store::transaction &, size_t batch_size);

	celerix::account next{ 0 };
	size_t completed{ 0 };
};

struct pending_database_scanner
{
	celerix::ledger & ledger;

	std::deque<celerix::account> next_batch (celerix::store::transaction &, size_t batch_size);

	celerix::account next{ 0 };
	size_t completed{ 0 };
};

class database_scan
{
public:
	explicit database_scan (celerix::ledger &);

	celerix::account next (std::function<bool (celerix::account const &)> const & filter);

	// Indicates if a full ledger iteration has taken place e.g. warmed up
	bool warmed_up () const;

	celerix::container_info container_info () const;

private: // Dependencies
	celerix::ledger & ledger;

private:
	void fill ();

private:
	account_database_scanner account_scanner;
	pending_database_scanner pending_scanner;

	std::deque<celerix::account> queue;

	static size_t constexpr batch_size = 512;
};
}
