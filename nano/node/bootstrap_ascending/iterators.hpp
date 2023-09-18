#pragma once

#include <nano/lib/numbers.hpp>

#include <deque>

namespace nano
{
namespace store
{
	class component;
}
class transaction;

namespace bootstrap_ascending
{
	class database_iterator
	{
	public:
		enum class table_type
		{
			account,
			pending
		};

		explicit database_iterator (nano::store::component & store, table_type);
		nano::account operator* () const;
		void next (nano::transaction & tx);

	private:
		nano::store::component & store;
		nano::account current{ 0 };
		const table_type table;
	};

	class buffered_iterator
	{
	public:
		explicit buffered_iterator (nano::store::component & store);
		nano::account operator* () const;
		nano::account next ();
		// Indicates if a full ledger iteration has taken place e.g. warmed up
		bool warmup () const;

	private:
		void fill ();

	private:
		nano::store::component & store;
		std::deque<nano::account> buffer;
		bool warmup_m{ true };

		database_iterator accounts_iterator;
		database_iterator pending_iterator;

		static std::size_t constexpr size = 1024;
	};
} // nano
} // bootstrap_ascending
