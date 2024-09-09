#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/store/tables.hpp>

#include <chrono>
#include <memory>

namespace nano::store
{
class transaction_impl
{
public:
	transaction_impl (nano::id_dispenser::id_t const store_id);
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;

	nano::id_dispenser::id_t const store_id;
};

class read_transaction_impl : public transaction_impl
{
public:
	explicit read_transaction_impl (nano::id_dispenser::id_t const store_id = 0);
	virtual void reset () = 0;
	virtual void renew () = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	explicit write_transaction_impl (nano::id_dispenser::id_t const store_id = 0);
	virtual void commit () = 0;
	virtual void renew () = 0;
	virtual bool contains (nano::tables table_a) const = 0;
};

class transaction
{
public:
	using epoch_t = size_t;

public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
	virtual nano::id_dispenser::id_t store_id () const = 0;

	epoch_t epoch () const;
	std::chrono::steady_clock::time_point timestamp () const;

protected:
	epoch_t current_epoch{ 0 };
	std::chrono::steady_clock::time_point start{};
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	nano::id_dispenser::id_t store_id () const override;

	void reset ();
	void renew ();
	void refresh ();
	void refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 });

private:
	std::unique_ptr<read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	nano::id_dispenser::id_t store_id () const override;

	void commit ();
	void renew ();
	void refresh ();
	void refresh_if_needed (std::chrono::milliseconds max_age = std::chrono::milliseconds{ 500 });
	bool contains (nano::tables table_a) const;

private:
	std::unique_ptr<write_transaction_impl> impl;
};
} // namespace nano::store
