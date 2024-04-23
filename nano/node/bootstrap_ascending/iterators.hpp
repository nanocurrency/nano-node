#pragma once

#include <nano/lib/numbers.hpp>

#include <deque>

namespace nano
{
class ledger;
}

namespace nano::secure
{
class transaction;
}

namespace nano::bootstrap_ascending
{
class database_iterator
{
public:
	enum class table_type
	{
		account,
		pending
	};

	explicit database_iterator (nano::ledger & ledger, table_type);
	nano::account operator* () const;
	void next (secure::transaction & tx);

private:
	nano::ledger & ledger;
	nano::account current{ 0 };
	const table_type table;
};

class buffered_iterator
{
public:
	explicit buffered_iterator (nano::ledger & ledger);
	nano::account operator* () const;
	nano::account next ();
	// Indicates if a full ledger iteration has taken place e.g. warmed up
	bool warmup () const;

private:
	void fill ();

private:
	nano::ledger & ledger;
	std::deque<nano::account> buffer;
	bool warmup_m{ true };

	database_iterator accounts_iterator;
	database_iterator pending_iterator;

	static std::size_t constexpr size = 1024;
};
} // nano::bootstrap_ascending
