#pragma once

#include <nano/store/transaction.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>
#include <nano/store/write_queue.hpp>

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

	// Conversion operator to const nano::store::unconfirmed_transaction&
	virtual operator const nano::store::unconfirmed_transaction & () const = 0;
};

class write_transaction : public transaction
{
	nano::store::write_transaction txn;
	nano::store::unconfirmed_write_transaction unconfirmed_txn;
	nano::store::write_guard guard;
	std::chrono::steady_clock::time_point start;

public:
	explicit write_transaction (nano::store::write_transaction && txn, nano::store::unconfirmed_write_transaction && unconfirmed_txn, nano::store::write_guard && guard) noexcept :
		txn{ std::move (txn) },
		unconfirmed_txn{ std::move (unconfirmed_txn) },
		guard{ std::move (guard) }
	{
		start = std::chrono::steady_clock::now ();
	}

	// Override to return a reference to the encapsulated write_transaction
	const nano::store::transaction & base_txn () const override
	{
		return txn;
	}

	void commit ()
	{
		txn.commit ();
		guard.release ();
	}

	void renew ()
	{
		guard.renew ();
		txn.renew ();
		start = std::chrono::steady_clock::now ();
	}

	void refresh ()
	{
		commit ();
		renew ();
	}

	bool refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 })
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
	
	operator const nano::store::unconfirmed_transaction & () const override
	{
		return unconfirmed_txn;
	}
	
	operator const nano::store::unconfirmed_write_transaction & () const
	{
		return unconfirmed_txn;
	}
};

class read_transaction : public transaction
{
	nano::store::read_transaction txn;
	nano::store::unconfirmed_read_transaction unconfirmed_txn;

public:
	explicit read_transaction (nano::store::read_transaction && t, nano::store::unconfirmed_read_transaction && u_t) noexcept :
		txn{ std::move (t) },
		unconfirmed_txn{ std::move (u_t) }
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

	void refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 })
	{
		txn.refresh_if_needed (max_age);
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

	// Conversion operator to const nano::store::transaction&
	operator const nano::store::unconfirmed_transaction & () const override
	{
		return unconfirmed_txn;
	}
};
} // namespace nano::secure
