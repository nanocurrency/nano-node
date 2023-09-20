#pragma once

#include <nano/store/peer.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class peer : public nano::store::peer
{
private:
	nano::store::rocksdb::component & store;

public:
	explicit peer (nano::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	void del (store::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	bool exists (store::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;
	store::iterator<nano::endpoint_key, nano::no_value> begin (store::transaction const & transaction_a) const override;
	store::iterator<nano::endpoint_key, nano::no_value> end () const override;
};
} // namespace nano::store::rocksdb
