#pragma once

#include <celerix/secure/account_info.hpp>
#include <celerix/secure/pending_info.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/pending.hpp>

#include <optional>

namespace celerix::bootstrap
{
struct account_database_crawler
{
	using value_type = std::pair<celerix::account, celerix::account_info>;

	static constexpr size_t sequential_attempts = 10;

	account_database_crawler (celerix::store::component & store, celerix::store::transaction const & transaction, celerix::account const & start) :
		store{ store },
		transaction{ transaction },
		it{ store.account.end (transaction) },
		end{ store.account.end (transaction) }
	{
		seek (start);
	}

	void seek (celerix::account const & account)
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

	void advance_to (celerix::account const & account)
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

	celerix::store::component & store;
	celerix::store::transaction const & transaction;

	celerix::store::account::iterator it;
	celerix::store::account::iterator const end;
};

struct pending_database_crawler
{
	using value_type = std::pair<celerix::pending_key, celerix::pending_info>;

	static constexpr size_t sequential_attempts = 10;

	pending_database_crawler (celerix::store::component & store, celerix::store::transaction const & transaction, celerix::account const & start) :
		store{ store },
		transaction{ transaction },
		it{ store.pending.end (transaction) },
		end{ store.pending.end (transaction) }
	{
		seek (start);
	}

	void seek (celerix::account const & account)
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
			seek (inc_sat (starting_account.number ()));
		}

		update_current ();
	}

	void advance_to (celerix::account const & account)
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

	celerix::store::component & store;
	celerix::store::transaction const & transaction;

	celerix::store::pending::iterator it;
	celerix::store::pending::iterator const end;
};
}