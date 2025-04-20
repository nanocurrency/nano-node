#pragma once

#include <nano/store/transaction.hpp>
#include <nano/store/write_queue.hpp>

#include <future>
#include <utility>

namespace nano::secure
{

class transaction
{
public:
	transaction () = default;
	virtual ~transaction () = default;

	// Deleting copy and assignment operations
	transaction (const transaction &) = delete;
	transaction & operator= (const transaction &) = delete;

	// Default move operations
	transaction (transaction &&) noexcept = default;
	transaction & operator= (transaction &&) noexcept = default;

	// Pure virtual function to get a const reference to the base store transaction
	virtual const nano::store::transaction & base_txn () const = 0;

	// Conversion operator to const nano::store::transaction&
	virtual operator const nano::store::transaction & () const = 0;

	// Certain transactions may need to be refreshed if they are held for a long time
	virtual bool refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 }) = 0;
};

class write_transaction final : public transaction
{
	nano::store::write_guard guard; // Guard should be released after the transaction
	nano::store::write_transaction txn;
	std::chrono::steady_clock::time_point start;

	// Future to signal transaction got committed
	std::promise<void> promise;
	std::shared_future<void> future{ promise.get_future () };

public:
	write_transaction (nano::store::write_transaction && txn_a, nano::store::write_guard && guard_a) noexcept :
		guard{ std::move (guard_a) },
		txn{ std::move (txn_a) },
		start{ std::chrono::steady_clock::now () }
	{
		debug_assert (active ());
	}

	~write_transaction () override
	{
		if (active ())
		{
			commit ();
		}
	}

	write_transaction (write_transaction && other) = default;

	// Override to return a reference to the encapsulated write_transaction
	const nano::store::transaction & base_txn () const override
	{
		return txn;
	}

	void commit ()
	{
		txn.commit ();
		guard.release ();
		promise.set_value ();
	}

	void renew ()
	{
		guard.renew ();
		txn.renew ();
		start = std::chrono::steady_clock::now ();
		promise = {};
		future = { promise.get_future () };
	}

	void refresh ()
	{
		commit ();
		renew ();
	}

	bool refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 }) override
	{
		auto now = std::chrono::steady_clock::now ();
		if (now - start > max_age)
		{
			refresh ();
			return true;
		}
		return false;
	}

	auto timestamp () const
	{
		return txn.timestamp ();
	}

	bool active () const
	{
		return guard.is_owned ();
	}

	std::shared_future<void> get_future () const
	{
		return future; // Give a copy of the shared future
	}

	// Conversion operator to const nano::store::transaction&
	operator const nano::store::transaction & () const override
	{
		return txn;
	}

	// Additional conversion operator specific to nano::store::write_transaction
	operator const nano::store::write_transaction & () const
	{
		return txn;
	}
};

class read_transaction final : public transaction
{
	nano::store::read_transaction txn;

public:
	explicit read_transaction (nano::store::read_transaction && t) noexcept :
		txn{ std::move (t) }
	{
	}

	// Override to return a reference to the encapsulated read_transaction
	const nano::store::transaction & base_txn () const override
	{
		return txn;
	}

	void refresh ()
	{
		txn.refresh ();
	}

	bool refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 }) override
	{
		return txn.refresh_if_needed (max_age);
	}

	auto timestamp () const
	{
		return txn.timestamp ();
	}

	// Conversion operator to const nano::store::transaction&
	operator const nano::store::transaction & () const override
	{
		return txn;
	}

	// Additional conversion operator specific to nano::store::read_transaction
	operator const nano::store::read_transaction & () const
	{
		return txn;
	}
};
}
