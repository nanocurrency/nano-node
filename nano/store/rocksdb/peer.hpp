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
	void put (store::write_transaction const &, nano::endpoint_key const & endpoint, nano::millis_t timestamp) override;
	nano::millis_t get (store::transaction const &, nano::endpoint_key const & endpoint) const override;
	void del (store::write_transaction const &, nano::endpoint_key const & endpoint) override;
	bool exists (store::transaction const &, nano::endpoint_key const & endpoint) const override;
	size_t count (store::transaction const &) const override;
	void clear (store::write_transaction const &) override;
	iterator begin (store::transaction const &) const override;
	iterator end (store::transaction const & transaction_a) const override;
};
} // namespace nano::store::rocksdb
