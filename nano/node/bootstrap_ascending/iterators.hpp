#pragma once

#include <nano/secure/account_info.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/pending.hpp>

#include <optional>

namespace nano::bootstrap_ascending
{
struct account_database_crawler
{
	using value_type = std::pair<nano::account, nano::account_info>;

	static constexpr size_t sequential_attempts = 10;

	account_database_crawler (nano::store::component & store, nano::store::transaction const & transaction, nano::account const & start) :
		store{ store },
		transaction{ transaction },
		it{ store.account.end () },
		end{ store.account.end () }
	{
		seek (start);
	}

	void seek (nano::account const & account)
	{
		it = store.account.begin (transaction, account);
		update_current ();
	}

	void advance ()
	{
		if (it == end)
		{
			debug_assert (!current);
			return;
		}

		++it;
		update_current ();
	}

	void advance_to (nano::account const & account)
	{
		if (it == end)
		{
			debug_assert (!current);
			return;
		}

		// First try advancing sequentially
		for (size_t count = 0; count < sequential_attempts && it != end; ++count, ++it)
		{
			// Break if we've reached or overshoot the target account
			if (it->first.number () >= account.number ())
			{
				update_current ();
				return;
			}
		}

		// If that fails, perform a fresh lookup
		seek (account);
	}

	std::optional<value_type> current{};

private:
	void update_current ()
	{
		if (it != end)
		{
			current = *it;
		}
		else
		{
			current = std::nullopt;
		}
	}

	nano::store::component & store;
	nano::store::transaction const & transaction;

	nano::store::account::iterator it;
	nano::store::account::iterator const end;
};

struct pending_database_crawler
{
	using value_type = std::pair<nano::pending_key, nano::pending_info>;

	static constexpr size_t sequential_attempts = 10;

	pending_database_crawler (nano::store::component & store, nano::store::transaction const & transaction, nano::account const & start) :
		store{ store },
		transaction{ transaction },
		it{ store.pending.end () },
		end{ store.pending.end () }
	{
		seek (start);
	}

	void seek (nano::account const & account)
	{
		it = store.pending.begin (transaction, { account, 0 });
		update_current ();
	}

	// Advance to the next account
	void advance ()
	{
		if (it == end)
		{
			debug_assert (!current);
			return;
		}

		auto const starting_account = it->first.account;

		// First try advancing sequentially
		for (size_t count = 0; count < sequential_attempts && it != end; ++count, ++it)
		{
			// Break if we've reached the next account
			if (it->first.account != starting_account)
			{
				update_current ();
				return;
			}
		}

		if (it != end)
		{
			// If that fails, perform a fresh lookup
			seek (starting_account.number () + 1);
		}

		update_current ();
	}

	void advance_to (nano::account const & account)
	{
		if (it == end)
		{
			debug_assert (!current);
			return;
		}

		// First try advancing sequentially
		for (size_t count = 0; count < sequential_attempts && it != end; ++count, ++it)
		{
			// Break if we've reached or overshoot the target account
			if (it->first.account.number () >= account.number ())
			{
				update_current ();
				return;
			}
		}

		// If that fails, perform a fresh lookup
		seek (account);
	}

	std::optional<value_type> current{};

private:
	void update_current ()
	{
		if (it != end)
		{
			current = *it;
		}
		else
		{
			current = std::nullopt;
		}
	}

	nano::store::component & store;
	nano::store::transaction const & transaction;

	nano::store::pending::iterator it;
	nano::store::pending::iterator const end;
};
}