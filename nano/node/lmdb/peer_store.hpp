#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class mdb_store;
class peer_store_mdb : public peer_store
{
private:
	nano::mdb_store & store;

public:
	explicit peer_store_mdb (nano::mdb_store & store_a);
	void put (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	void del (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	bool exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const override;
	size_t count (nano::transaction const & transaction_a) const override;
	void clear (nano::write_transaction const & transaction_a) override;
	nano::store_iterator<nano::endpoint_key, nano::no_value> begin (nano::transaction const & transaction_a) const override;
	nano::store_iterator<nano::endpoint_key, nano::no_value> end () const override;
};
}
