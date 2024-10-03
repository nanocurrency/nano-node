#pragma once

#include <nano/store/transaction.hpp>

namespace nano::store
{
class unconfirmed_transaction
{
public:
	virtual operator nano::store::transaction const & () const = 0;
};
class unconfirmed_read_transaction : public unconfirmed_transaction
{
public:
	explicit unconfirmed_read_transaction (std::unique_ptr<read_transaction_impl> && read_transaction_impl);

	operator nano::store::transaction const & () const override
	{
		return *tx;
	}
private:
	std::unique_ptr<read_transaction> tx;
};
class unconfirmed_write_transaction : public unconfirmed_transaction
{
public:
	explicit unconfirmed_write_transaction (std::unique_ptr<write_transaction_impl> && write_transaction_impl);

	operator nano::store::transaction const & () const override
	{
		return *tx;
	}

	operator nano::store::write_transaction const & () const
	{
		return *tx;
	}
private:
	std::unique_ptr<write_transaction> tx;
};
}
